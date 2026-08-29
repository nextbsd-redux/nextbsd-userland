/*
 * libnotify_selfcontain.c — definitions libnotify.so.1 needs to load.
 *
 * notify_client.c is compiled INTO libnotify, and its crash-time logging
 * path references two symbols:
 *
 *	_simple_asl_log()	 (notify_client.c:341)
 *	OS_BUG_INTERNAL()	 (via NOTIFY_INTERNAL_CRASH, notify_internal.h:41)
 *
 * Both were only ever defined in freebsd-shims/notifyd_stubs.c, which is
 * compiled into notifyd, syslogd, aslmanager and util -- never into the
 * library itself. So libnotify.so.1 shipped with two undefined symbols:
 *
 *	$ nm -D libnotify.so.1 | grep -E 'OS_BUG_INTERNAL|_simple_asl_log'
 *	         U OS_BUG_INTERNAL
 *	         U _simple_asl_log
 *
 * Those four executables happened to paper over it by linking the stubs
 * statically, so nothing noticed. Any OTHER client of libnotify dies at
 * load time:
 *
 *	ld-elf.so.1: /usr/lib/system/libnotify.so.1:
 *	    Undefined symbol "_simple_asl_log"
 *
 * which is exactly how the #91 lost-wakeup probe failed -- it reported a
 * verdict for a poke whose binary never started.
 *
 * A library must resolve its own references, so the definitions belong
 * here. The four executables above still compile notifyd_stubs.c; an
 * executable's definition preempts the shared library's under normal ELF
 * symbol resolution, so their behaviour is unchanged.
 *
 * Semantics are copied verbatim from notifyd_stubs.c -- deliberately
 * no-ops, not new policy. See #91.
 */

/*
 * Apple's _simple-library malloc-free ASL logger. notify_client.c calls it
 * on the crash path, where allocating or re-entering ASL would be unsafe;
 * dropping the message is the safe behaviour.
 */
void
_simple_asl_log(int level, const char *facility, const char *message)
{
	(void)level; (void)facility; (void)message;
}

/*
 * "Soft bug" marker (os/log). Signature matches the
 * NOTIFY_INTERNAL_CRASH(c, x) -> OS_BUG_INTERNAL(c, "LIBNOTIFY", x) macro.
 */
void
OS_BUG_INTERNAL(unsigned long code, const char *subsystem, const char *msg)
{
	(void)code; (void)subsystem; (void)msg;
}
