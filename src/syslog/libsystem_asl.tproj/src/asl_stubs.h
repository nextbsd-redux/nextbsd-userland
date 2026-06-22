/*
 * asl_stubs.h — prototypes for the os_activity / os_log / mbr runtime stubs
 * defined in asl_stubs.c (linked into libsystem_asl.so).
 *
 * Declared so the libsystem_asl consumers (asl.c, syslog.c, …) see prototypes
 * — else -Werror=implicit-function-declaration on releng/15.1. Force-included
 * via the Makefile so every TU gets them. Prototypes use the underlying types
 * (uint64_t / void *) rather than the os_activity_* typedefs to avoid any
 * typedef clash with asl_stubs.c's own definitions.
 */
#ifndef _ASL_FREEBSD_STUBS_H_
#define _ASL_FREEBSD_STUBS_H_

#include <stdint.h>
#include <stdarg.h>	/* va_list */

uint64_t os_activity_get_identifier(void *activity, uint64_t *parent_id);
int      os_log_shim_enabled(void *log);
/* asl.c passes a va_list (va_copy'd ap) as `args` — must be va_list, not void*. */
void     os_log_with_args_4syslog(void *log, uint8_t type, const char *format,
             va_list args, void *ret_addr);
int      mbr_check_membership(void *user, void *group, int *ismember);

#endif /* !_ASL_FREEBSD_STUBS_H_ */
