/*
 * os_alloc_once.c — a once-allocator that is actually once.
 *
 * The shim this replaces was a static inline that did:
 *
 *	void *p = calloc(1, sz);
 *	if (p && init) init(p);
 *	return p;
 *
 * unconditionally, ignoring the token. Its own comment said "allocating once
 * per slot" and "uses a malloc'd block on first call", so it read as correct
 * at every call site while being the opposite.
 *
 * What that cost, in libnotify (notify_client.c:534, _notify_globals(), called
 * at the top of every public entry point):
 *
 *   - Every API call got a FRESH ZEROED globals struct. notify_server_port was
 *     therefore always MACH_PORT_NULL, so every call re-ran the whole
 *     _notify_lib_init path: bootstrap_look_up2, the checkin RPC, and
 *     _notify_generate_common_port. One logical RPC became four, each an
 *     independent chance to block, and all of them untimed.
 *   - registration_table and name_node_table were empty on entry, so
 *     notify_check(), notify_cancel() and notify_get_state() could never find
 *     a token they had themselves returned. They silently did nothing.
 *   - notifyd allocated a brand-new Mach port per client call in
 *     _notify_generate_common_port, leaking ports and port_table entries.
 *   - A globals struct plus two hash tables leaked on every call.
 *
 * Definition lives in a .c, not the header, deliberately: the slot table must
 * be one object per library. A static inline gives every translation unit its
 * own copy, which is the same bug wearing a different hat -- two TUs would
 * hand out two different "singletons".
 */

#include <sys/cdefs.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <os/alloc_once_private.h>

static void *os_alloc_once_slots[OS_ALLOC_ONCE_KEY_MAX + 1];
static pthread_mutex_t os_alloc_once_mtx = PTHREAD_MUTEX_INITIALIZER;

void *
os_alloc_once(os_alloc_token_t token, size_t sz, void (*init)(void *))
{
	void *p;

	if ((unsigned)token > OS_ALLOC_ONCE_KEY_MAX) {
		fprintf(stderr, "os_alloc_once: token %d out of range\n",
		    (int)token);
		abort();
	}

	/*
	 * Fast path. Publication below happens with the mutex held and the
	 * store is the last thing done, so a non-NULL read here always refers
	 * to a fully initialised block.
	 */
	p = __atomic_load_n(&os_alloc_once_slots[token], __ATOMIC_ACQUIRE);
	if (p != NULL)
		return p;

	pthread_mutex_lock(&os_alloc_once_mtx);
	p = os_alloc_once_slots[token];
	if (p == NULL) {
		p = calloc(1, sz);
		if (p == NULL) {
			/*
			 * Returning NULL would be worse than dying: every
			 * caller here dereferences the result immediately
			 * (_notify_globals, _launch_globals), so a NULL turns
			 * into a null-deref one frame up with no clue why.
			 */
			pthread_mutex_unlock(&os_alloc_once_mtx);
			fprintf(stderr, "os_alloc_once: out of memory "
			    "allocating %zu bytes for slot %d\n", sz,
			    (int)token);
			abort();
		}
		if (init != NULL)
			init(p);
		/* Publish only after init() has run. */
		__atomic_store_n(&os_alloc_once_slots[token], p,
		    __ATOMIC_RELEASE);
	}
	pthread_mutex_unlock(&os_alloc_once_mtx);

	return p;
}
