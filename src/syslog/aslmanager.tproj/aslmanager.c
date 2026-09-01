/*
 * Copyright (c) 2007-2015 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#include <asl.h>
#include <asl_msg.h>
#include <asl_msg_list.h>
#include <asl_store.h>
#include <errno.h>
#include <vproc_priv.h>
#include <os/transaction_private.h>
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <unistd.h>

#include "asl_common.h"
#include "daemon.h"

/* global */
bool dryrun;
uint32_t debug;
FILE *debugfp;
dispatch_queue_t work_queue;

static time_t module_ttl;
static dispatch_source_t sig_term_src;

/* wait 5 minutes to run main task after being invoked by XPC */
#define MAIN_TASK_INITIAL_DELAY 300

/*
 * Used to set config parameters.
 * Line format "= name value"
 */
static void
_aslmanager_set_param(asl_out_dst_data_t *dst, char *s)
{
	char **l;
	uint32_t count;

	if (s == NULL) return;
	if (s[0] == '\0') return;

	/* skip '=' and whitespace */
	if (*s == '=') s++;
	while ((*s == ' ') || (*s == '\t')) s++;

	l = explode(s, " \t");
	if (l == NULL) return;

	for (count = 0; l[count] != NULL; count++);

	/* name is required */
	if (count == 0)
	{
		free_string_list(l);
		return;
	}

	/* value is required */
	if (count == 1)
	{
		free_string_list(l);
		return;
	}

	if (!strcasecmp(l[0], "aslmanager_debug"))
	{
		/* = debug level */
		set_debug(DEBUG_ASL, l[1]);
	}
	else if (!strcasecmp(l[0], "store_ttl"))
	{
		/* = store_ttl days */
		dst->ttl[LEVEL_ALL] = asl_core_str_to_time(l[1], SECONDS_PER_DAY);
	}
	else if (!strcasecmp(l[0], "module_ttl"))
	{
		/* = module_ttl days */
		module_ttl = asl_core_str_to_time(l[1], SECONDS_PER_DAY);
	}
	else if (!strcasecmp(l[0], "max_store_size"))
	{
		/* = max_file_size bytes */
		dst->all_max = asl_core_str_to_size(l[1]);
	}
	else if (!strcasecmp(l[0], "archive"))
	{
		free(dst->rotate_dir);
		dst->rotate_dir = NULL;

		/* = archive {0|1} path */
		if (!strcmp(l[1], "1"))
		{
			if (l[2] == NULL) dst->rotate_dir = strdup(PATH_ASL_ARCHIVE);
			else dst->rotate_dir = strdup(l[2]);
		}
	}
	else if (!strcasecmp(l[0], "store_path"))
	{
		/* = archive path */
		free(dst->path);
		dst->path = strdup(l[1]);
	}
	else if (!strcasecmp(l[0], "archive_mode"))
	{
		dst->mode = strtol(l[1], NULL, 0);
		if ((dst->mode == 0) && (errno == EINVAL)) dst->mode = 0400;
	}

	free_string_list(l);
}

int
cli_main(int argc, char *argv[])
{
	int i, work;
	asl_out_module_t *mod, *m;
	asl_out_rule_t *r;
	asl_out_dst_data_t store, opts, *asl_store_dst = NULL;
	const char *mname = NULL;
	bool quiet = false;

#if !TARGET_OS_SIMULATOR
	if (geteuid() != 0)
	{
		if (argc == 0) debug = DEBUG_ASL;
		else debug = DEBUG_STDERR;

		debug_log(ASL_LEVEL_ERR, "aslmanager must be run by root\n");
		exit(1);
	}
#endif

	module_ttl = DEFAULT_TTL;

	/* cobble up a dst_data with defaults and parameter settings */
	memset(&store, 0, sizeof(store));
	store.ttl[LEVEL_ALL] = DEFAULT_TTL;
	store.all_max = DEFAULT_MAX_SIZE;

	memset(&opts, 0, sizeof(opts));
	opts.ttl[LEVEL_ALL] = DEFAULT_TTL;
	opts.all_max = DEFAULT_MAX_SIZE;

	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-q"))
		{
			quiet = true;
		}
		else if (!strcmp(argv[i], "-dd"))
		{
			quiet = true;
		}
		else if (!strcmp(argv[i], "-s"))
		{
			if (((i + 1) < argc) && (argv[i + 1][0] != '-'))
			{
				store.path = strdup(argv[++i]);
				asl_store_dst = &store;
			}
		}
	}

	if (!quiet)
	{
		char *path = NULL;
		int status = asl_make_database_dir(NULL, NULL);
		if (status == 0) status = asl_make_database_dir(ASL_INTERNAL_LOGS_DIR, &path);
		if (status == 0)
		{
			char tstamp[32], *str = NULL;

			asl_make_timestamp(time(NULL), MODULE_NAME_STYLE_STAMP_LCL_B, tstamp, sizeof(tstamp));
			asprintf(&str, "%s/aslmanager.%s", path, tstamp);

			if (str != NULL)
			{
				if (status == 0) debugfp = fopen(str, "w");
				if (debugfp != NULL) debug |= DEBUG_FILE;
				free(str);
			}
		}
		free(path);
	}

	/* get parameters from asl.conf */
	mod = asl_out_module_init();

	if (mod != NULL)
	{
		for (r = mod->ruleset; (r != NULL) && (asl_store_dst == NULL); r = r->next)
		{
			if ((r->dst != NULL) && (r->action == ACTION_OUT_DEST) && (!strcmp(r->dst->path, PATH_ASL_STORE)))
				asl_store_dst = r->dst;
		}

		for (r = mod->ruleset; r != NULL; r = r->next)
		{
			if (r->action == ACTION_SET_PARAM)
			{
				if (r->query == NULL) _aslmanager_set_param(asl_store_dst, r->options);
			}
		}
	}

	work = DO_ASLDB | DO_MODULE;

	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-a"))
		{
			if (asl_store_dst == NULL) asl_store_dst = &store;

			if (((i + 1) < argc) && (argv[i + 1][0] != '-')) asl_store_dst->rotate_dir = strdup(argv[++i]);
			else asl_store_dst->rotate_dir = strdup(PATH_ASL_ARCHIVE);
			asl_store_dst->mode = 0400;
		}
		else if (!strcmp(argv[i], "-store_ttl"))
		{
			if (((i + 1) < argc) && (argv[i + 1][0] != '-'))
			{
				if (asl_store_dst == NULL) asl_store_dst = &store;
				asl_store_dst->ttl[LEVEL_ALL] = asl_core_str_to_time(argv[++i], SECONDS_PER_DAY);
			}
		}
		else if (!strcmp(argv[i], "-module_ttl"))
		{
			if (((i + 1) < argc) && (argv[i + 1][0] != '-')) module_ttl = asl_core_str_to_time(argv[++i], SECONDS_PER_DAY);
		}
		else if (!strcmp(argv[i], "-ttl"))
		{
			if (((i + 1) < argc) && (argv[i + 1][0] != '-'))
			{
				opts.ttl[LEVEL_ALL] = asl_core_str_to_time(argv[++i], SECONDS_PER_DAY);

				if (asl_store_dst == NULL) asl_store_dst = &store;
				asl_store_dst->ttl[LEVEL_ALL] = opts.ttl[LEVEL_ALL];

				module_ttl = opts.ttl[LEVEL_ALL];
			}
		}
		else if (!strcmp(argv[i], "-size"))
		{
			if (((i + 1) < argc) && (argv[i + 1][0] != '-'))
			{
				opts.all_max = asl_core_str_to_size(argv[++i]);

				if (asl_store_dst == NULL) asl_store_dst = &store;
				asl_store_dst->all_max = opts.all_max;
			}
		}
		else if (!strcmp(argv[i], "-checkpoint"))
		{
			work |= DO_CHECKPT;
		}
		else if (!strcmp(argv[i], "-module"))
		{
			work &= ~DO_ASLDB;

			/* optional name follows -module */
			if ((i +1) < argc)
			{
				if (argv[i + 1][0] != '-') mname = argv[++i];
			}
		}
		else if (!strcmp(argv[i], "-asldb"))
		{
			work = DO_ASLDB;
		}
		else if (!strcmp(argv[i], "-d"))
		{
			if (((i + i) < argc) && (argv[i+1][0] != '-')) set_debug(DEBUG_STDERR, argv[++i]);
			else set_debug(DEBUG_STDERR, NULL);
		}
		else if (!strcmp(argv[i], "-dd"))
		{
			dryrun = true;

			if (((i + i) < argc) && (argv[i+1][0] != '-')) set_debug(DEBUG_STDERR, argv[++i]);
			else set_debug(DEBUG_STDERR, "l7");
		}
	}

	if (asl_store_dst != NULL && asl_store_dst->path == NULL) asl_store_dst->path = strdup(PATH_ASL_STORE);

	debug_log(ASL_LEVEL_ERR, "aslmanager starting%s\n", dryrun ? " dryrun" : "");

	if (work & DO_ASLDB) process_asl_data_store(asl_store_dst, &opts);

	if (work & DO_MODULE)
	{
		if (work & DO_CHECKPT) checkpoint(mname);

		if (mod != NULL)
		{
			for (m = mod; m != NULL; m = m->next)
			{
				if (mname == NULL)
				{
					process_module(m, NULL);
				}
				else if ((m->name != NULL) && (!strcmp(m->name, mname)))
				{
					process_module(m, &opts);
				}
			}
		}
	}

	asl_out_module_free(mod);

	debug_log(ASL_LEVEL_NOTICE, "----------------------------------------\n");
	debug_log(ASL_LEVEL_ERR, "aslmanager finished%s\n", dryrun ? " dryrun" : "");
	debug_close();

	return 0;
}




/*
 * run_managed — the launchd-managed path. Does not return.
 *
 * WHAT THIS RESTORES
 *
 * Apple's aslmanager branched on VPROC_GSK_IS_MANAGED: unmanaged ran cli_main()
 * and exited, managed built an XPC listener on com.apple.aslmanager and called
 * dispatch_main() -- so a managed aslmanager was demand-launched ONCE and then
 * stayed resident for the life of the boot, servicing triggers in place. That
 * is still how it behaves on macOS today: `ps` shows aslmanager up from boot,
 * from a plist with no RunAtLoad, no KeepAlive and no StartInterval.
 *
 * #143 deleted that branch along with the XPC it was written in, and #164
 * replaced it with drain-work-exit. Both lost the residency. This restores it
 * in the transport that was underneath XPC all along.
 *
 * WHY BARE MACH AND NOT XPC
 *
 * launchd demand-launches on a MACH message; XPC was only the envelope Apple
 * wrapped that in for 10.9. Dropping the envelope removes libxpc (#152) and,
 * more importantly, libdispatch from this path: dispatch_main() and
 * xpc_connection_* both route through our libdispatch, whose dispatch_async
 * SIGSEGVs in dx_push from arbitrary pthreads (asl_action.c:1765). A plain
 * blocking mach_msg() receive loop has neither problem, and is the same shape
 * notifyd already uses in this tree.
 *
 * COALESCING
 *
 * After each wakeup we drain whatever else is queued before working. Apple did
 * the same thing with main_task()'s enqueue/delay machinery -- a burst of
 * triggers should produce one rotation pass, not one per message. Rotation is
 * idempotent, so the only cost of a missed coalesce is wasted work.
 */
static void
run_managed(mach_port_t port)
{
	kern_return_t kr;
	int drained;

	fprintf(stderr, "ASLMANAGER-RESIDENT: pid=%d listening on %s\n",
	    (int)getpid(), ASLMANAGER_SERVICE_NAME);
	fflush(stderr);

	for (;;)
	{
		union {
			mach_msg_header_t hdr;
			char buf[1024];
		} m;

		/*
		 * Block indefinitely. This is the whole point: the process stays
		 * alive between triggers instead of exiting and making launchd
		 * fork a new one per rotation. launchd stops us with SIGTERM at
		 * shutdown, and EnablePressuredExit in the plist lets it reclaim
		 * us under memory pressure.
		 */
		kr = mach_msg(&m.hdr, MACH_RCV_MSG, 0, (mach_msg_size_t)sizeof(m),
		    port, MACH_MSG_TIMEOUT_NONE, MACH_PORT_NULL);
		if (kr != KERN_SUCCESS)
		{
			/*
			 * A receive error on our own service port is not something
			 * we can recover from by looping -- that would spin. Log
			 * and exit; launchd will demand-launch a fresh instance on
			 * the next trigger.
			 */
			fprintf(stderr, "aslmanager: mach_msg(RCV) failed kr=0x%x; "
			    "exiting\n", (unsigned)kr);
			fflush(stderr);
			return;
		}
		mach_msg_destroy(&m.hdr);

		/* Coalesce a burst: take everything already queued. */
		drained = 1;
		for (;;)
		{
			union {
				mach_msg_header_t hdr;
				char buf[1024];
			} extra;

			kr = mach_msg(&extra.hdr, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0,
			    (mach_msg_size_t)sizeof(extra), port, 0, MACH_PORT_NULL);
			if (kr != KERN_SUCCESS) break;
			mach_msg_destroy(&extra.hdr);
			drained++;
		}

		fprintf(stderr, "ASLMANAGER-TRIGGERED: pid=%d coalesced=%d\n",
		    (int)getpid(), drained);
		fflush(stderr);

		(void)cli_main(0, NULL);
	}
}

int
main(int argc, char *argv[])
{
	mach_port_t port = MACH_PORT_NULL;
	kern_return_t kr;

	setiopolicy_np(IOPOL_TYPE_DISK, IOPOL_SCOPE_PROCESS, IOPOL_THROTTLE);

	/*
	 * Managed or not? Apple asked vproc_swap_integer(VPROC_GSK_IS_MANAGED);
	 * checking in for the service answers the same question more directly
	 * and with the thing we actually need. bootstrap_check_in() only
	 * succeeds for the job that DECLARES the service in its plist, so
	 * success means "launchd started us for com.apple.aslmanager" and hands
	 * us the receive right in one step.
	 *
	 * Failure means someone ran /usr/sbin/aslmanager from a shell. That must
	 * keep working -- it is how the tool is used by hand and by aslmanager(8)
	 * -- so fall through to the one-shot CLI, which is what unmanaged did on
	 * Apple too.
	 */
	kr = bootstrap_check_in(bootstrap_port, ASLMANAGER_SERVICE_NAME, &port);
	if (kr == KERN_SUCCESS && port != MACH_PORT_NULL)
	{
		run_managed(port);	/* only returns on an unrecoverable error */
		return 0;
	}

	fprintf(stderr, "aslmanager: bootstrap_check_in(%s) = %d; running one-shot\n",
	    ASLMANAGER_SERVICE_NAME, (int)kr);
	fflush(stderr);

	return cli_main(argc, argv);
}
