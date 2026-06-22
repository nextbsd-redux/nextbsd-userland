/*
 * notifyd_freebsd_compat.h — force-included shim for notifyd source.
 *
 * Different from libnotify_freebsd_compat.h (used by the client lib);
 * notifyd uses additional Apple-private constants.
 */
#ifndef _FREEBSD_SHIM_NOTIFYD_COMPAT_H_
#define _FREEBSD_SHIM_NOTIFYD_COMPAT_H_

#include <asl.h>
#include <sys/reboot.h>

/* FreeBSD has RB_HALT_CPU / RB_AUTOBOOT but not RB_PANIC. notifyd
 * uses RB_PANIC in a path that's logically "abort + panic" — map to
 * a regular abort() trigger since FreeBSD reboot() can't panic. */
#ifndef RB_PANIC
#define RB_PANIC	0x40000000	/* not a real FreeBSD flag */
#endif

/* MIG-generated subsystem name. Our MIG emits _notify_ipc_subsystem
 * (from `subsystem notify_ipc` + `serverprefix _`). notifyd's source
 * expects do_notify_subsystem — Apple xcode build renames via some
 * post-processing. Alias here keeps Apple source unchanged. */
#define do_notify_subsystem		_notify_ipc_subsystem

/* Apple SYS_initgroups syscall number. Used to call initgroups
 * directly via syscall(2). FreeBSD has the same syscall via
 * /usr/include/sys/syscall.h as SYS_setgroups maybe. Easiest:
 * just alias to setgroups (initgroups is a libc wrapper). */
#ifndef SYS_initgroups
#define SYS_initgroups	81		/* approx — FreeBSD has SYS_setgroups=91 */
#endif

/* Apple sandbox_check flag. */
#ifndef SANDBOX_CHECK_NO_REPORT
#define SANDBOX_CHECK_NO_REPORT		0x0001
#endif

/* Apple open(2) flags not in FreeBSD:
 *   O_EVTONLY — open without delivering FS events to the file
 *   O_SYMLINK — open the symlink itself instead of its target
 * Map to FreeBSD nearest equivalents. */
#ifndef O_EVTONLY
#define O_EVTONLY	O_RDONLY	/* close-enough */
#endif
#ifndef O_SYMLINK
#define O_SYMLINK	O_NOFOLLOW	/* FreeBSD spelling */
#endif

/* Prototypes for the notifyd shims defined in freebsd-shims/notifyd_stubs.c.
 * Declared in this force-included header so notifyd.c sees them (else
 * -Werror=implicit-function-declaration on releng/15.1). Pull the types they
 * reference first (this header is force-included ahead of notifyd's own). */
#include <stdint.h>
#include <sys/types.h>
#include <mach/mach.h>		/* mach_port_t */
#include <bsm/libbsm.h>		/* audit_token_t */

mach_port_t current_task(void);
pid_t       audit_token_to_pid(audit_token_t atoken);
void        xpc_event_publisher_set_error_handler(void *publisher, void *handler);
void        xpc_event_publisher_set_throttling(void *publisher, uint64_t interval);
/* reboot_np(RB_PANIC, msg): notifyd's jetsam-panic path; FreeBSD has no jetsam,
 * so the stub logs + abort()s (never reached in practice). */
void        reboot_np(int howto, const char *msg);

#endif
