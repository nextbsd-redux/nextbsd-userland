/*
 * os/collections.h — FreeBSD port of Apple's intrusive set/map types used by
 * libnotify's notify_state_t.
 *
 * These were previously stubbed: every op was a no-op and every lookup
 * returned NULL, with a comment saying real behaviour was still owed. That
 * shipped into a running notifyd, where it is not a missing feature but a
 * crash: _nc_table_delete_n() asserts that deleting a key returns the object
 * being freed, so a find/delete that always answers NULL aborts the daemon on
 * the first teardown of any registration. See nextbsd-userland#120.
 *
 * This is a real implementation: chained hash tables, one per key flavour.
 *
 * INTRUSIVE, like Apple's. A set stores a pointer to the KEY FIELD embedded in
 * the caller's payload struct, and table.c recovers the payload by subtracting
 * a key_offset. So the set never owns or copies anything, and an element must
 * not be freed while still inserted -- exactly the contract the callers in
 * notifyd already follow.
 *
 * The three flavours are distinct incomplete struct types rather than void *,
 * which is what lets the generic macros below dispatch on key type.
 */
#ifndef _FREEBSD_SHIM_OS_COLLECTIONS_H_
#define _FREEBSD_SHIM_OS_COLLECTIONS_H_

#include <sys/cdefs.h>
#include <sys/queue.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

__BEGIN_DECLS

struct nc_set_str;
struct nc_set_u32;
struct nc_set_u64;
struct nc_map_u64;

typedef struct nc_set_str *os_set_str_ptr_t;
typedef struct nc_set_u32 *os_set_32_ptr_t;
typedef struct nc_set_u64 *os_set_64_ptr_t;
typedef struct nc_set_str *os_set_ptr_t;

typedef struct nc_map_u64 *os_map_64_t;

/*
 * Block signatures match what table.c passes: the block receives the stored
 * key pointer, and returns true to continue iterating (false stops).
 *
 * Iteration runs over a snapshot of the element pointers, because callers do
 * mutate during a walk -- _notify_lib_regenerate_registration() re-registers
 * from inside _nc_table_foreach_n().
 */
typedef bool (^nc_set_str_walker_t)(const char **);
typedef bool (^nc_set_u32_walker_t)(uint32_t *);
typedef bool (^nc_set_u64_walker_t)(uint64_t *);

/* --- string-keyed set --------------------------------------------------- */
void   nc_set_str_init(os_set_str_ptr_t *s);
void   nc_set_str_destroy(os_set_str_ptr_t *s);
void   nc_set_str_insert(os_set_str_ptr_t *s, void *keyptr);
void  *nc_set_str_find(os_set_str_ptr_t *s, const char *key);
void  *nc_set_str_delete(os_set_str_ptr_t *s, const char *key);
size_t nc_set_str_count(os_set_str_ptr_t *s);
void   nc_set_str_foreach(os_set_str_ptr_t *s, nc_set_str_walker_t w);

/* --- uint32-keyed set --------------------------------------------------- */
void   nc_set_u32_init(os_set_32_ptr_t *s);
void   nc_set_u32_destroy(os_set_32_ptr_t *s);
void   nc_set_u32_insert(os_set_32_ptr_t *s, void *keyptr);
void  *nc_set_u32_find(os_set_32_ptr_t *s, uint32_t key);
void  *nc_set_u32_delete(os_set_32_ptr_t *s, uint32_t key);
size_t nc_set_u32_count(os_set_32_ptr_t *s);
void   nc_set_u32_foreach(os_set_32_ptr_t *s, nc_set_u32_walker_t w);

/* --- uint64-keyed set --------------------------------------------------- */
void   nc_set_u64_init(os_set_64_ptr_t *s);
void   nc_set_u64_destroy(os_set_64_ptr_t *s);
void   nc_set_u64_insert(os_set_64_ptr_t *s, void *keyptr);
void  *nc_set_u64_find(os_set_64_ptr_t *s, uint64_t key);
void  *nc_set_u64_delete(os_set_64_ptr_t *s, uint64_t key);
size_t nc_set_u64_count(os_set_64_ptr_t *s);
void   nc_set_u64_foreach(os_set_64_ptr_t *s, nc_set_u64_walker_t w);

/* --- uint64-keyed map (notifyd's event_table) --------------------------- */
void   nc_map_u64_init(os_map_64_t *m);
void   nc_map_u64_destroy(os_map_64_t *m);
void   nc_map_u64_insert(os_map_64_t *m, uint64_t key, void *value);
void  *nc_map_u64_find(os_map_64_t *m, uint64_t key);
void  *nc_map_u64_delete(os_map_64_t *m, uint64_t key);
size_t nc_map_u64_count(os_map_64_t *m);

__END_DECLS

/*
 * Generic entry points, dispatching on the set's key flavour. The `ops`
 * argument Apple's os_set_init() takes is unused here: callers pass NULL and
 * these tables have no per-instance callbacks.
 */
#define os_set_init(s, ops)	(void)(ops), _Generic(*(s),			\
	os_set_str_ptr_t: nc_set_str_init,					\
	os_set_32_ptr_t:  nc_set_u32_init,					\
	os_set_64_ptr_t:  nc_set_u64_init)(s)

#define os_set_destroy(s)	_Generic(*(s),					\
	os_set_str_ptr_t: nc_set_str_destroy,					\
	os_set_32_ptr_t:  nc_set_u32_destroy,					\
	os_set_64_ptr_t:  nc_set_u64_destroy)(s)

#define os_set_insert(s, k)	_Generic(*(s),					\
	os_set_str_ptr_t: nc_set_str_insert,					\
	os_set_32_ptr_t:  nc_set_u32_insert,					\
	os_set_64_ptr_t:  nc_set_u64_insert)(s, (void *)(k))

#define os_set_find(s, k)	_Generic(*(s),					\
	os_set_str_ptr_t: nc_set_str_find,					\
	os_set_32_ptr_t:  nc_set_u32_find,					\
	os_set_64_ptr_t:  nc_set_u64_find)(s, k)

#define os_set_delete(s, k)	_Generic(*(s),					\
	os_set_str_ptr_t: nc_set_str_delete,					\
	os_set_32_ptr_t:  nc_set_u32_delete,					\
	os_set_64_ptr_t:  nc_set_u64_delete)(s, k)

#define os_set_count(s)		_Generic(*(s),					\
	os_set_str_ptr_t: nc_set_str_count,					\
	os_set_32_ptr_t:  nc_set_u32_count,					\
	os_set_64_ptr_t:  nc_set_u64_count)(s)

#define os_set_foreach(s, blk)	_Generic(*(s),					\
	os_set_str_ptr_t: nc_set_str_foreach,					\
	os_set_32_ptr_t:  nc_set_u32_foreach,					\
	os_set_64_ptr_t:  nc_set_u64_foreach)(s, blk)

#define os_map_init(m, ops)	((void)(ops), nc_map_u64_init(m))
#define os_map_destroy(m)	nc_map_u64_destroy(m)
#define os_map_insert(m, k, v)	nc_map_u64_insert((m), (k), (void *)(v))
#define os_map_find(m, k)	nc_map_u64_find((m), (k))
#define os_map_get(m, k)	nc_map_u64_find((m), (k))
#define os_map_delete(m, k)	nc_map_u64_delete((m), (k))
#define os_map_count(m)		nc_map_u64_count(m)

#endif
