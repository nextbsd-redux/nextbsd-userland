/* os/alloc_once_private.h — FreeBSD shim. Apple's slot-indexed
 * once-init allocation. Used for libsystem-wide singletons. Map to
 * pthread_once + heap allocation. */
#ifndef _FREEBSD_SHIM_OS_ALLOC_ONCE_PRIVATE_H_
#define _FREEBSD_SHIM_OS_ALLOC_ONCE_PRIVATE_H_

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Apple defines os_alloc_token_* enum slots; pick high numbers we
 * don't conflict with. Real impl on Apple uses a fixed table; we
 * just allocate-on-demand per slot. */
typedef int os_alloc_token_t;

/* Apple's predefined slot keys. We just pick numbers; the slot table
 * is per-process and we malloc on demand instead of using a real
 * table, so collisions don't matter. */
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_NOTIFY	1
#define OS_ALLOC_ONCE_KEY_LIBXPC		2
#define OS_ALLOC_ONCE_KEY_LIBDISPATCH		3
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_C		4
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_INFO	5
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_NETWORK	6
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_ASL		7
#define OS_ALLOC_ONCE_KEY_LIBSYSTEM_TRACE	8
#define OS_ALLOC_ONCE_KEY_MAX			32

/*
 * Generic once-allocator: one zero-initialised block per slot, init() run
 * exactly once, for the life of the process.
 *
 * DECLARED here, DEFINED in freebsd-shims/os_alloc_once.c. That split is
 * deliberate. The previous version was a static inline that called
 * calloc()+init() unconditionally and ignored the token, so every caller got
 * a fresh struct -- see os_alloc_once.c for what that cost libnotify. Making
 * it a static inline again would reintroduce the same class of bug even if
 * the body were correct, because each translation unit would get its own
 * slot table and hand out its own "singleton".
 */
void *os_alloc_once(os_alloc_token_t token, size_t sz, void (*init)(void *));

#endif
