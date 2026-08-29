/*
 * notifypoke — post one notification, so a test can wake notifyd on demand,
 * and say precisely WHY if it cannot.
 *
 * Exists for the lost-wakeup measurement in #91, which has now twice
 * reported a verdict for a poke that never happened: first because it fell
 * back to logger(1) (which posts to syslogd, not notifyd), then because
 * libnotify.so.1 had unresolvable symbols and this binary died at load.
 *
 * The third failure was notify_post() returning 1000000. That is
 * NOTIFY_STATUS_FAILED, a catch-all: notify_client.c collapses every status
 * >= 11 into it via IS_INTERNAL_ERROR(), so the real error is discarded
 * before the caller sees it. Two paths inside notify_post can produce it --
 * regenerate_check() and _notify_lib_init(EVENT_INIT) -- and the latter is a
 * bootstrap_look_up2() for NOTIFY_SERVICE_NAME with
 * BOOTSTRAP_PRIVILEGED_SERVER.
 *
 * So probe the lookup DIRECTLY here, with flags = 0, before calling
 * notify_post. That splits the three candidates apart:
 *
 *   lookup fails            -> notifyd is not registered / not running
 *   lookup ok, post fails   -> the privileged-lookup path is the problem
 *                              (bootstrap_look_up3 decides on an audit token
 *                              that nothing may be populating)
 *   both ok, notifyd idle   -> a genuine lost wakeup, which is the thing
 *                              #91 set out to measure
 *
 * <notify.h> is deliberately not included: it pulls the os/ header pack,
 * which is not on the test-build include path. Only notify_post is needed
 * and its signature is stable (notify_client.c:2007).
 *
 * usage: notifypoke <name>	exit 0 only if the post succeeded
 */
#include <mach/mach.h>
#include <servers/bootstrap.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern uint32_t notify_post(const char *name);

#define NOTIFY_STATUS_OK	0
#define NOTIFY_SERVICE_NAME	"com.apple.system.notification_center"

int
main(int argc, char **argv)
{
	const char *name = (argc > 1) ? argv[1] : "com.apple.system.notifypoke";
	mach_port_t sp = MACH_PORT_NULL;
	kern_return_t kr;
	uint32_t st;

	/*
	 * Make libnotify print the internal status it would otherwise destroy.
	 * IS_INTERNAL_ERROR() collapses every code >= 11 into
	 * NOTIFY_STATUS_FAILED, and the real one goes only to ASL -- the very
	 * subsystem that is wedged when this matters. Set before any libnotify
	 * call so the first failure is already covered.
	 */
	setenv("LIBNOTIFY_DEBUG_ERRORS", "1", 1);

	printf("notifypoke: bootstrap_port=0x%x\n", (unsigned)bootstrap_port);

	kr = bootstrap_look_up(bootstrap_port, NOTIFY_SERVICE_NAME, &sp);
	printf("notifypoke: bootstrap_look_up(%s) kr=0x%x port=0x%x\n",
	    NOTIFY_SERVICE_NAME, (unsigned)kr, (unsigned)sp);
	if (kr != KERN_SUCCESS)
		printf("notifypoke: NOTIFYD-LOOKUP-FAIL\n");
	else
		printf("notifypoke: NOTIFYD-LOOKUP-OK\n");

	st = notify_post(name);
	if (st != NOTIFY_STATUS_OK) {
		printf("notifypoke: notify_post(%s) failed: %u%s\n", name, st,
		    (st == 1000000) ? " (NOTIFY_STATUS_FAILED -- real code"
		    " discarded by IS_INTERNAL_ERROR)" : "");
		return (1);
	}
	printf("notifypoke: posted %s\n", name);
	return (0);
}
