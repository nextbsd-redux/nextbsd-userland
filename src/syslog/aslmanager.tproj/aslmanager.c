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




#define ASLMANAGER_SERVICE_NAME "com.apple.aslmanager"

/*
 * drain_launch_trigger — consume the demand-launch doorbell.
 *
 * launchd starts this job when a message arrives on the
 * com.apple.aslmanager service port, and bootstrap_check_in() hands us the
 * receive right launchd holds for it. If we exit WITHOUT draining, the
 * message stays queued, mportset_callback() (runtime.c:541) sees a non-zero
 * mps_msgcount on the next pass and launches us again -- a respawn loop
 * bounded only by launchd's throttle. Draining is what makes exit safe.
 *
 * The message is a doorbell and nothing more. Apple's XPC listener ignored
 * the request dictionary too ("Some day, we may use the dictionary to pass
 * parameters to aslmanager, but for now, we ignore the input"), so there is
 * nothing to parse: the work is always cli_main().
 *
 * Non-fatal throughout. A failed check-in means we were started some other
 * way -- by hand, or from the command line -- which is a normal way to run
 * this tool, and the rotation work is still correct. MACH_RCV_TIMEOUT with a
 * zero timeout means we never block: we take what is queued and leave.
 */
static void
drain_launch_trigger(void)
{
	mach_port_t port = MACH_PORT_NULL;
	kern_return_t kr;
	int drained = 0;

	kr = bootstrap_check_in(bootstrap_port, ASLMANAGER_SERVICE_NAME, &port);
	if (kr != KERN_SUCCESS)
	{
		fprintf(stderr, "aslmanager: bootstrap_check_in(%s) = %d; not "
		    "demand-launched, running the rotation anyway\n",
		    ASLMANAGER_SERVICE_NAME, (int)kr);
		return;
	}

	for (;;)
	{
		union {
			mach_msg_header_t hdr;
			char buf[1024];
		} m;

		kr = mach_msg(&m.hdr, MACH_RCV_MSG | MACH_RCV_TIMEOUT, 0,
		    (mach_msg_size_t)sizeof(m), port, 0, MACH_PORT_NULL);
		if (kr != KERN_SUCCESS) break;

		drained++;
		mach_msg_destroy(&m.hdr);
	}

	/*
	 * This line IS the probe's evidence. StandardErrorPath in the plist
	 * routes it to /var/log/aslmanager.stderr, which the boot suite reads:
	 * `launchctl list` cannot answer "did this job ever run", because
	 * job_export() (core.c:1095) always inserts LastExitStatus, so a job
	 * that never ran prints the same "-  0" as one that exited cleanly.
	 */
	fprintf(stderr, "ASLMANAGER-DEMAND-LAUNCHED: service=%s pid=%d "
	    "drained=%d last_kr=0x%x\n", ASLMANAGER_SERVICE_NAME, (int)getpid(),
	    drained, (unsigned)kr);
	fflush(stderr);
}

int
main(int argc, char *argv[])
{
	/*
	 * Periodic model (nextbsd-userland#143), now demand-launched again.
	 *
	 * This used to branch on VPROC_GSK_IS_MANAGED: unmanaged ran cli_main()
	 * and exited, managed set up an XPC listener on com.apple.aslmanager and
	 * called dispatch_main() forever, doing work only when a client sent a
	 * message. The client half was asl_trigger_aslmanager() in
	 * libsystem_asl, a synchronous XPC round-trip -- which is what stalled
	 * syslogd for 30s on the boot path before it bound /var/run/log (#87).
	 *
	 * The XPC halves stay gone. What comes back is the launchd mechanism
	 * underneath them, which was never XPC: launchd demand-launches on a
	 * MACH message, and #143 discarded that along with the envelope. We
	 * drain the doorbell, do the work, and exit -- no listener, no
	 * dispatch_main(), no libxpc, and no libdispatch on this path (our
	 * dispatch_async SIGSEGVs from arbitrary pthreads, asl_action.c:1765).
	 */
	setiopolicy_np(IOPOL_TYPE_DISK, IOPOL_SCOPE_PROCESS, IOPOL_THROTTLE);

	drain_launch_trigger();

	return cli_main(argc, argv);
}
