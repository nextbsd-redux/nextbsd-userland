/*
 * notifypoke — post one notification, so a test can wake notifyd on demand.
 *
 * Exists for the lost-wakeup measurement in #91. The first version of that
 * test fell back to logger(1) when notifyutil was unavailable, which was
 * useless: logger posts to SYSLOGD via /var/run/log and never touches
 * notifyd, so the test answered a question nobody asked.
 *
 * <notify.h> is deliberately NOT included. It pulls <os/base.h>, which is not
 * on the test-build include path, and this needs exactly one function whose
 * signature is stable. Declaring it directly keeps the test binary free of
 * the os/ header pack.
 *
 * usage: notifypoke <name>          exit 0 on success, 1 on failure
 */
#include <stdint.h>
#include <stdio.h>

extern uint32_t notify_post(const char *name);

#define NOTIFY_STATUS_OK 0

int
main(int argc, char **argv)
{
	uint32_t st;
	const char *name = (argc > 1) ? argv[1] : "com.apple.system.notifypoke";

	st = notify_post(name);
	if (st != NOTIFY_STATUS_OK) {
		printf("notifypoke: notify_post(%s) failed: %u\n", name, st);
		return (1);
	}
	printf("notifypoke: posted %s\n", name);
	return (0);
}
