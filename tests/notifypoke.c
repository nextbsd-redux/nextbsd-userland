/*
 * notifypoke — post one notification, so a test can wake notifyd on demand.
 *
 * Exists for the lost-wakeup measurement in #91. The first version of that
 * test fell back to logger(1) when notifyutil was unavailable, which was
 * useless: logger posts to SYSLOGD via /var/run/log and never touches
 * notifyd, so the test answered a question nobody asked.
 *
 * notify_post() is the whole job. notifyutil(1) exists in the tree but has no
 * Makefile and carries entitlement plumbing this does not need.
 *
 * usage: notifypoke <name>          exit 0 on success, 1 on failure
 */
#include <notify.h>
#include <stdio.h>

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
