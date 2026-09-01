/*
 * test_demand_launch.c — does launchd demand-launch a MachServices job?
 *
 * A discovery probe, not a regression test. It answers one question that
 * nothing else on the image answers, and that decides how aslmanager should
 * be driven (nextbsd-userland#143 follow-up):
 *
 *   Is on-demand Mach-service launch working today?
 *
 * When #143 removed aslmanager's MachServices in favour of StartInterval, the
 * answer was no -- #79 had found mach_port_get_set_status() and
 * mach_port_get_attributes() were both stubs, so launchd's mportset_callback()
 * (runtime.c:541) was an unconditional no-op with no log line. #83 replaced
 * those stubs on 08-29; #143 landed 08-31 22:56. Nobody re-ran the path in
 * between, so the removal was argued from a failure that may already have been
 * fixed.
 *
 * What this does: look the service up through launchd's bootstrap, send ONE
 * one-way Mach message to it, and report. It deliberately does not wait for a
 * reply -- the aslmanager trigger is advisory by contract (every error path in
 * asl_trigger_aslmanager() returned success), so a doorbell is the whole
 * protocol. No reply means no semaphore, no timeout stall, and no
 * dispatch_async: the three things that made the XPC version dangerous on
 * syslogd's recv pthread (asl_action.c:1765).
 *
 * Whether aslmanager actually RAN is not decided here -- the caller checks
 * /var/log/aslmanager.stderr for that, because `launchctl list` cannot tell a
 * job that never ran from one that exited 0 (job_export(), core.c:1095, always
 * inserts LastExitStatus).
 *
 * Exit status: 0 if the message was sent, 1 otherwise. Markers on stdout.
 */

#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Must match ASLMANAGER_TRIGGER_MSG_ID in aslcommon/asl_common.h, which is the
 * value asl_trigger_aslmanager() sends. Duplicated rather than included because
 * asl_common.h is an internal header and is not installed to /usr/include; the
 * receiver ignores the id either way, so a drift here costs only the ability to
 * recognise the message in a trace. */
#define TRIGGER_MSG_ID	0x41534c4d	/* 'ASLM' */

/* Never block. If the service port's queue is full the doorbell has already
 * been rung and not yet answered, which is a pass, not a hang. */
#define SEND_TIMEOUT_MS	2000

int
main(int argc, char *argv[])
{
	const char *service = (argc > 1) ? argv[1] : "com.apple.aslmanager";
	mach_port_t port = MACH_PORT_NULL;
	mach_msg_header_t msg;
	kern_return_t kr;
	mach_msg_return_t mr;

	if (bootstrap_port == MACH_PORT_NULL) {
		printf("DEMAND-LOOKUP-FAIL: bootstrap_port is MACH_PORT_NULL "
		    "(no launchd bootstrap in this task)\n");
		return 1;
	}

	kr = bootstrap_look_up(bootstrap_port, (char *)service, &port);
	if (kr != KERN_SUCCESS || port == MACH_PORT_NULL) {
		/*
		 * A failure here is about the SERVICE not being registered --
		 * i.e. launchd never parsed MachServices for the job, or the
		 * job is not loaded. It says nothing yet about demand launch.
		 */
		printf("DEMAND-LOOKUP-FAIL: bootstrap_look_up(%s) kr=0x%x "
		    "port=0x%x\n", service, (unsigned)kr, (unsigned)port);
		return 1;
	}
	printf("DEMAND-LOOKUP-OK: %s -> port 0x%x\n", service, (unsigned)port);

	memset(&msg, 0, sizeof(msg));
	msg.msgh_bits = MACH_MSGH_BITS(MACH_MSG_TYPE_COPY_SEND, 0);
	msg.msgh_size = sizeof(msg);
	msg.msgh_remote_port = port;
	msg.msgh_local_port = MACH_PORT_NULL;
	msg.msgh_id = TRIGGER_MSG_ID;

	mr = mach_msg(&msg, MACH_SEND_MSG | MACH_SEND_TIMEOUT, sizeof(msg), 0,
	    MACH_PORT_NULL, SEND_TIMEOUT_MS, MACH_PORT_NULL);
	if (mr != MACH_MSG_SUCCESS) {
		printf("DEMAND-SEND-FAIL: mach_msg(%s) mr=0x%x\n", service,
		    (unsigned)mr);
		(void)mach_port_deallocate(mach_task_self(), port);
		return 1;
	}

	printf("DEMAND-SEND-OK: doorbell delivered to %s (id=0x%x)\n", service,
	    TRIGGER_MSG_ID);
	(void)mach_port_deallocate(mach_task_self(), port);
	return 0;
}
