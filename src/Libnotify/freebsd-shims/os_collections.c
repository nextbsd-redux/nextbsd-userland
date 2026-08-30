/*
 * os_collections.c — real implementation of the os_set / os_map primitives
 * libnotify's table.c and notifyd are written against.
 *
 * These were stubbed to no-ops returning NULL. In notifyd that is not a
 * missing feature, it is a crash: _nc_table_delete_n() does
 *
 *	uint32_t *result = os_set_delete(&t->set, key);
 *	os_assert(result == expected);
 *
 * so a delete that always answers NULL aborts the daemon the first time any
 * registration is torn down. That is nextbsd-userland#120 — notifyd dying with
 * "Assertion failed: (result == expected), function _nc_table_delete_n".
 * Every lookup silently missing also meant notifyd could never find an
 * existing registration, so it created duplicates instead of reusing them.
 *
 * Chained hash table, one flavour per key type. The tables are INTRUSIVE in
 * the same way Apple's are: what is stored is a pointer to the key field
 * inside the caller's payload, and table.c recovers the payload by
 * subtracting key_offset. Nothing here owns, copies or frees payloads.
 */

#include <sys/cdefs.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <os/collections.h>

#define NC_INITIAL_BUCKETS	16

struct nc_entry {
	void		*keyptr;	/* -> key field inside the payload */
	struct nc_entry	*next;
};

struct nc_table {
	struct nc_entry	**buckets;
	size_t		 nbuckets;
	size_t		 count;
};

/* The four public handles are all a table; distinct tags exist so the generic
 * macros in the header can dispatch on key flavour. */
struct nc_set_str { struct nc_table t; };
struct nc_set_u32 { struct nc_table t; };
struct nc_set_u64 { struct nc_table t; };

struct nc_map_entry {
	uint64_t		key;
	void			*value;
	struct nc_map_entry	*next;
};

struct nc_map_u64 {
	struct nc_map_entry	**buckets;
	size_t			 nbuckets;
	size_t			 count;
};

/*
 * Allocation failure here means a few dozen bytes were unavailable. Returning
 * quietly would put the table back into exactly the silently-wrong state this
 * file exists to remove -- a missing insert becomes a failed lookup becomes
 * the os_assert above. Fail loudly instead; the surrounding notifyd code takes
 * the same line (NOTIFY_INTERNAL_CRASH on malloc failure).
 */
static void *
nc_xalloc(size_t n)
{
	void *p = calloc(1, n);

	if (p == NULL) {
		fprintf(stderr, "libnotify: os_collections: out of memory "
		    "allocating %zu bytes\n", n);
		abort();
	}
	return p;
}

static size_t
nc_hash_str(const char *s)
{
	/* FNV-1a */
	size_t h = (size_t)0xcbf29ce484222325ULL;

	while (*s != '\0') {
		h ^= (unsigned char)*s++;
		h *= (size_t)0x100000001b3ULL;
	}
	return h;
}

static size_t
nc_hash_u64(uint64_t k)
{
	/* splitmix64 finalizer -- cheap and mixes the low bits, which matters
	 * because our keys are pids and mach port names: small, dense, and
	 * otherwise all landing in the first few buckets. */
	k += 0x9e3779b97f4a7c15ULL;
	k = (k ^ (k >> 30)) * 0xbf58476d1ce4e5b9ULL;
	k = (k ^ (k >> 27)) * 0x94d049bb133111ebULL;
	return (size_t)(k ^ (k >> 31));
}

/* ------------------------------------------------------------- set core -- */

static void
nc_table_init(struct nc_table *t)
{
	t->nbuckets = NC_INITIAL_BUCKETS;
	t->count = 0;
	t->buckets = nc_xalloc(t->nbuckets * sizeof(*t->buckets));
}

typedef size_t (*nc_keyhash_fn)(const void *keyptr);
typedef bool   (*nc_keyeq_fn)(const void *keyptr, const void *key);

static void
nc_table_grow(struct nc_table *t, nc_keyhash_fn hash)
{
	size_t nnew = t->nbuckets * 2;
	struct nc_entry **nb = nc_xalloc(nnew * sizeof(*nb));

	for (size_t i = 0; i < t->nbuckets; i++) {
		struct nc_entry *e = t->buckets[i];

		while (e != NULL) {
			struct nc_entry *next = e->next;
			size_t b = hash(e->keyptr) % nnew;

			e->next = nb[b];
			nb[b] = e;
			e = next;
		}
	}
	free(t->buckets);
	t->buckets = nb;
	t->nbuckets = nnew;
}

static void
nc_table_insert(struct nc_table *t, void *keyptr, nc_keyhash_fn hash)
{
	if (t->buckets == NULL)
		nc_table_init(t);
	if (t->count + 1 > t->nbuckets * 2)
		nc_table_grow(t, hash);

	struct nc_entry *e = nc_xalloc(sizeof(*e));
	size_t b = hash(keyptr) % t->nbuckets;

	e->keyptr = keyptr;
	e->next = t->buckets[b];
	t->buckets[b] = e;
	t->count++;
}

static void *
nc_table_find(struct nc_table *t, const void *key, size_t khash,
    nc_keyeq_fn eq)
{
	if (t == NULL || t->buckets == NULL)
		return NULL;

	for (struct nc_entry *e = t->buckets[khash % t->nbuckets];
	    e != NULL; e = e->next) {
		if (eq(e->keyptr, key))
			return e->keyptr;
	}
	return NULL;
}

static void *
nc_table_delete(struct nc_table *t, const void *key, size_t khash,
    nc_keyeq_fn eq)
{
	if (t == NULL || t->buckets == NULL)
		return NULL;

	struct nc_entry **pp = &t->buckets[khash % t->nbuckets];

	while (*pp != NULL) {
		struct nc_entry *e = *pp;

		if (eq(e->keyptr, key)) {
			void *kp = e->keyptr;

			*pp = e->next;
			free(e);
			t->count--;
			return kp;
		}
		pp = &e->next;
	}
	return NULL;
}

/*
 * Snapshot the element pointers before walking. Callers mutate mid-iteration
 * -- _notify_lib_regenerate_registration() re-registers from inside
 * _nc_table_foreach_n() -- and walking the live chains while they are being
 * rehashed would read freed entries.
 */
static void **
nc_table_snapshot(struct nc_table *t, size_t *n_out)
{
	if (t == NULL || t->buckets == NULL || t->count == 0) {
		*n_out = 0;
		return NULL;
	}

	void **v = nc_xalloc(t->count * sizeof(*v));
	size_t n = 0;

	for (size_t i = 0; i < t->nbuckets && n < t->count; i++) {
		for (struct nc_entry *e = t->buckets[i]; e != NULL;
		    e = e->next) {
			if (n >= t->count)
				break;
			v[n++] = e->keyptr;
		}
	}
	*n_out = n;
	return v;
}

static void
nc_table_destroy(struct nc_table *t)
{
	if (t == NULL || t->buckets == NULL)
		return;
	for (size_t i = 0; i < t->nbuckets; i++) {
		struct nc_entry *e = t->buckets[i];

		while (e != NULL) {
			struct nc_entry *next = e->next;

			free(e);
			e = next;
		}
	}
	free(t->buckets);
	t->buckets = NULL;
	t->nbuckets = 0;
	t->count = 0;
}

/* ------------------------------------------------------- string-keyed -- */

static size_t
hash_str_entry(const void *keyptr)
{
	return nc_hash_str(*(const char * const *)keyptr);
}

static bool
eq_str_entry(const void *keyptr, const void *key)
{
	const char *stored = *(const char * const *)keyptr;

	if (stored == NULL || key == NULL)
		return stored == key;
	return strcmp(stored, (const char *)key) == 0;
}

void
nc_set_str_init(os_set_str_ptr_t *s)
{
	*s = nc_xalloc(sizeof(**s));
	nc_table_init(&(*s)->t);
}

void
nc_set_str_destroy(os_set_str_ptr_t *s)
{
	if (*s == NULL)
		return;
	nc_table_destroy(&(*s)->t);
	free(*s);
	*s = NULL;
}

void
nc_set_str_insert(os_set_str_ptr_t *s, void *keyptr)
{
	if (*s == NULL)
		nc_set_str_init(s);
	nc_table_insert(&(*s)->t, keyptr, hash_str_entry);
}

void *
nc_set_str_find(os_set_str_ptr_t *s, const char *key)
{
	if (*s == NULL || key == NULL)
		return NULL;
	return nc_table_find(&(*s)->t, key, nc_hash_str(key), eq_str_entry);
}

void *
nc_set_str_delete(os_set_str_ptr_t *s, const char *key)
{
	if (*s == NULL || key == NULL)
		return NULL;
	return nc_table_delete(&(*s)->t, key, nc_hash_str(key), eq_str_entry);
}

size_t
nc_set_str_count(os_set_str_ptr_t *s)
{
	return (*s == NULL) ? 0 : (*s)->t.count;
}

void
nc_set_str_foreach(os_set_str_ptr_t *s, nc_set_str_walker_t w)
{
	if (*s == NULL)
		return;

	size_t n;
	void **v = nc_table_snapshot(&(*s)->t, &n);

	for (size_t i = 0; i < n; i++) {
		if (!w((const char **)v[i]))
			break;
	}
	free(v);
}

/* ------------------------------------------------------- uint32-keyed -- */

static size_t
hash_u32_entry(const void *keyptr)
{
	return nc_hash_u64(*(const uint32_t *)keyptr);
}

static bool
eq_u32_entry(const void *keyptr, const void *key)
{
	return *(const uint32_t *)keyptr == *(const uint32_t *)key;
}

void
nc_set_u32_init(os_set_32_ptr_t *s)
{
	*s = nc_xalloc(sizeof(**s));
	nc_table_init(&(*s)->t);
}

void
nc_set_u32_destroy(os_set_32_ptr_t *s)
{
	if (*s == NULL)
		return;
	nc_table_destroy(&(*s)->t);
	free(*s);
	*s = NULL;
}

void
nc_set_u32_insert(os_set_32_ptr_t *s, void *keyptr)
{
	if (*s == NULL)
		nc_set_u32_init(s);
	nc_table_insert(&(*s)->t, keyptr, hash_u32_entry);
}

void *
nc_set_u32_find(os_set_32_ptr_t *s, uint32_t key)
{
	if (*s == NULL)
		return NULL;
	return nc_table_find(&(*s)->t, &key, nc_hash_u64(key), eq_u32_entry);
}

void *
nc_set_u32_delete(os_set_32_ptr_t *s, uint32_t key)
{
	if (*s == NULL)
		return NULL;
	return nc_table_delete(&(*s)->t, &key, nc_hash_u64(key), eq_u32_entry);
}

size_t
nc_set_u32_count(os_set_32_ptr_t *s)
{
	return (*s == NULL) ? 0 : (*s)->t.count;
}

void
nc_set_u32_foreach(os_set_32_ptr_t *s, nc_set_u32_walker_t w)
{
	if (*s == NULL)
		return;

	size_t n;
	void **v = nc_table_snapshot(&(*s)->t, &n);

	for (size_t i = 0; i < n; i++) {
		if (!w((uint32_t *)v[i]))
			break;
	}
	free(v);
}

/* ------------------------------------------------------- uint64-keyed -- */

static size_t
hash_u64_entry(const void *keyptr)
{
	return nc_hash_u64(*(const uint64_t *)keyptr);
}

static bool
eq_u64_entry(const void *keyptr, const void *key)
{
	return *(const uint64_t *)keyptr == *(const uint64_t *)key;
}

void
nc_set_u64_init(os_set_64_ptr_t *s)
{
	*s = nc_xalloc(sizeof(**s));
	nc_table_init(&(*s)->t);
}

void
nc_set_u64_destroy(os_set_64_ptr_t *s)
{
	if (*s == NULL)
		return;
	nc_table_destroy(&(*s)->t);
	free(*s);
	*s = NULL;
}

void
nc_set_u64_insert(os_set_64_ptr_t *s, void *keyptr)
{
	if (*s == NULL)
		nc_set_u64_init(s);
	nc_table_insert(&(*s)->t, keyptr, hash_u64_entry);
}

void *
nc_set_u64_find(os_set_64_ptr_t *s, uint64_t key)
{
	if (*s == NULL)
		return NULL;
	return nc_table_find(&(*s)->t, &key, nc_hash_u64(key), eq_u64_entry);
}

void *
nc_set_u64_delete(os_set_64_ptr_t *s, uint64_t key)
{
	if (*s == NULL)
		return NULL;
	return nc_table_delete(&(*s)->t, &key, nc_hash_u64(key), eq_u64_entry);
}

size_t
nc_set_u64_count(os_set_64_ptr_t *s)
{
	return (*s == NULL) ? 0 : (*s)->t.count;
}

void
nc_set_u64_foreach(os_set_64_ptr_t *s, nc_set_u64_walker_t w)
{
	if (*s == NULL)
		return;

	size_t n;
	void **v = nc_table_snapshot(&(*s)->t, &n);

	for (size_t i = 0; i < n; i++) {
		if (!w((uint64_t *)v[i]))
			break;
	}
	free(v);
}

/* ------------------------------------------- uint64 -> void * map ------- */

void
nc_map_u64_init(os_map_64_t *m)
{
	*m = nc_xalloc(sizeof(**m));
	(*m)->nbuckets = NC_INITIAL_BUCKETS;
	(*m)->count = 0;
	(*m)->buckets = nc_xalloc((*m)->nbuckets * sizeof(*(*m)->buckets));
}

void
nc_map_u64_destroy(os_map_64_t *m)
{
	if (*m == NULL)
		return;
	for (size_t i = 0; i < (*m)->nbuckets; i++) {
		struct nc_map_entry *e = (*m)->buckets[i];

		while (e != NULL) {
			struct nc_map_entry *next = e->next;

			free(e);
			e = next;
		}
	}
	free((*m)->buckets);
	free(*m);
	*m = NULL;
}

static void
nc_map_grow(struct nc_map_u64 *m)
{
	size_t nnew = m->nbuckets * 2;
	struct nc_map_entry **nb = nc_xalloc(nnew * sizeof(*nb));

	for (size_t i = 0; i < m->nbuckets; i++) {
		struct nc_map_entry *e = m->buckets[i];

		while (e != NULL) {
			struct nc_map_entry *next = e->next;
			size_t b = nc_hash_u64(e->key) % nnew;

			e->next = nb[b];
			nb[b] = e;
			e = next;
		}
	}
	free(m->buckets);
	m->buckets = nb;
	m->nbuckets = nnew;
}

void
nc_map_u64_insert(os_map_64_t *m, uint64_t key, void *value)
{
	if (*m == NULL)
		nc_map_u64_init(m);

	struct nc_map_u64 *mm = *m;

	/* Replace an existing binding rather than shadowing it: notifyd checks
	 * for a duplicate before inserting (register_xpc_event crashes on one),
	 * so reaching here twice for a key should not silently leak the old
	 * entry. */
	size_t b = nc_hash_u64(key) % mm->nbuckets;

	for (struct nc_map_entry *e = mm->buckets[b]; e != NULL; e = e->next) {
		if (e->key == key) {
			e->value = value;
			return;
		}
	}

	if (mm->count + 1 > mm->nbuckets * 2) {
		nc_map_grow(mm);
		b = nc_hash_u64(key) % mm->nbuckets;
	}

	struct nc_map_entry *e = nc_xalloc(sizeof(*e));

	e->key = key;
	e->value = value;
	e->next = mm->buckets[b];
	mm->buckets[b] = e;
	mm->count++;
}

void *
nc_map_u64_find(os_map_64_t *m, uint64_t key)
{
	if (*m == NULL)
		return NULL;

	struct nc_map_u64 *mm = *m;

	for (struct nc_map_entry *e = mm->buckets[nc_hash_u64(key) % mm->nbuckets];
	    e != NULL; e = e->next) {
		if (e->key == key)
			return e->value;
	}
	return NULL;
}

void *
nc_map_u64_delete(os_map_64_t *m, uint64_t key)
{
	if (*m == NULL)
		return NULL;

	struct nc_map_u64 *mm = *m;
	struct nc_map_entry **pp = &mm->buckets[nc_hash_u64(key) % mm->nbuckets];

	while (*pp != NULL) {
		struct nc_map_entry *e = *pp;

		if (e->key == key) {
			void *v = e->value;

			*pp = e->next;
			free(e);
			mm->count--;
			return v;
		}
		pp = &e->next;
	}
	return NULL;
}

size_t
nc_map_u64_count(os_map_64_t *m)
{
	return (*m == NULL) ? 0 : (*m)->count;
}
