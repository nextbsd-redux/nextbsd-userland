/*
 * mach/port.h — core Mach port type vocabulary.
 *
 * Apple's <mach/port.h> is the base header defining the port name /
 * port right / port type vocabulary; <mach/mach_port.h> (the
 * port-management RPC layer) includes it. Several Apple-source
 * headers — notably libdispatch's <dispatch/source.h> — pull in
 * <mach/port.h> directly for just the mach_port_t type.
 *
 * Our split keeps mach_port_name_t / MACH_PORT_NULL in
 * <mach/mach_traps.h> and mach_port_t in <mach/message.h>; this
 * header re-exports both and adds the remaining base-vocabulary
 * constants (MACH_PORT_DEAD, the MACH_PORT_TYPE_* set) so a
 * consumer that includes only <mach/port.h> gets the full base
 * surface, matching Apple.
 */
#ifndef _MACH_PORT_H_
#define _MACH_PORT_H_

#include <stdint.h>
#include <mach/mach_traps.h>	/* mach_port_name_t, MACH_PORT_NULL */
#include <mach/message.h>	/* mach_port_t */
#include <mach/mach_port.h>	/* mach_port_right_t, MACH_PORT_RIGHT_* */

#ifndef MACH_PORT_DEAD
#define MACH_PORT_DEAD		((mach_port_name_t)~0)
#endif

#define MACH_PORT_VALID(name) \
	(((name) != MACH_PORT_NULL) && ((name) != MACH_PORT_DEAD))

/*
 * MACH_PORT_INDEX / MACH_PORT_GEN — split a mach_port_name_t into its
 * kernel-table index and per-name generation counter.
 *
 * These MUST match sys/mach/port.h in the kernel. They did not: the
 * comment described Apple's canonical layout (index low, generation
 * high) while the code implemented the opposite (index high, generation
 * low), and neither matched what this kernel actually hands out.
 *
 * This kernel puts the generation in the HIGH byte and the index in the
 * low 24 bits, because a port name is currently also a file descriptor
 * and the index half has to stay a valid fd number. (When port names
 * stop being file descriptors, both sides move to Apple's layout
 * together -- these macros and the kernel's, in one change.)
 *
 * Consequence of the old definition, and the reason this is not a
 * cosmetic fix: with `(name) & ~0xff` and a kernel that allocates small
 * sequential names (0x10, 0x11, 0x12, ...), MACH_PORT_INDEX returned
 * ZERO for every port on the system. Every consumer that hashes by it --
 * launchd's HASH_PORT, libdispatch's VL_HASH -- put every port in bucket
 * zero and degraded to a linear scan.
 *
 * Generation bits are all zero today, so for current names the value
 * returned by MACH_PORT_INDEX is unchanged from the raw name. The fix is
 * therefore behaviour-preserving for name IDENTITY and behaviour-fixing
 * for hash DISTRIBUTION.
 */
#define MACH_PORT_INDEX_MASK	0x00ffffffU
#define MACH_PORT_INDEX(name)	((mach_port_name_t)(name) & MACH_PORT_INDEX_MASK)
#define MACH_PORT_GEN(name)	((mach_port_name_t)(name) & 0xff000000U)
#define MACH_PORT_MAKE(idx, gen) \
	(((mach_port_name_t)(idx) & MACH_PORT_INDEX_MASK) | \
	 ((mach_port_name_t)(gen) & 0xff000000U))

/* mach_port_array_t — an out-of-line array of port names, the shape
 * MIG hands back from RPCs that return port lists (e.g. the launchd
 * job interface's lookup_children). */
typedef mach_port_t *mach_port_array_t;
#ifndef _MACH_PORT_NAME_ARRAY_T_
#define _MACH_PORT_NAME_ARRAY_T_
typedef mach_port_name_t *mach_port_name_array_t;
#endif

/* mach_port_type_t — a bitmask describing which rights a name holds.
 * The MACH_PORT_RIGHT_* values come from <mach/mach_port.h>. */
typedef natural_t mach_port_type_t;
typedef mach_port_type_t *mach_port_type_array_t;

#define MACH_PORT_TYPE(right)		((mach_port_type_t)(1 << ((right) + 16)))
#define MACH_PORT_TYPE_NONE		((mach_port_type_t)0)
#define MACH_PORT_TYPE_SEND		MACH_PORT_TYPE(MACH_PORT_RIGHT_SEND)
#define MACH_PORT_TYPE_RECEIVE		MACH_PORT_TYPE(MACH_PORT_RIGHT_RECEIVE)
#define MACH_PORT_TYPE_SEND_ONCE	MACH_PORT_TYPE(MACH_PORT_RIGHT_SEND_ONCE)
#define MACH_PORT_TYPE_PORT_SET		MACH_PORT_TYPE(MACH_PORT_RIGHT_PORT_SET)
#define MACH_PORT_TYPE_DEAD_NAME	MACH_PORT_TYPE(MACH_PORT_RIGHT_DEAD_NAME)

/*
 * mach_port_type — return the rights bitmask for the named port.
 * libdispatch (event_kevent.c:289) uses it as a coarse name-validity
 * probe. Declared here (not in <mach/mach_traps.h>) because
 * mach_port_type_t lives in this file; declaring it in mach_traps.h
 * would need a circular #include guard that mach_traps.h's prior
 * recursion through port.h breaks.
 */
kern_return_t mach_port_type(mach_port_name_t task, mach_port_name_t name,
    mach_port_type_t *type);

typedef natural_t mach_port_urefs_t;
#ifndef _MACH_PORT_DELTA_T_
#define _MACH_PORT_DELTA_T_
typedef integer_t mach_port_delta_t;
#endif
#ifndef _MACH_PORT_SEQNO_T_
#define _MACH_PORT_SEQNO_T_
typedef natural_t mach_port_seqno_t;
#endif
#ifndef _MACH_PORT_MSCOUNT_T_
#define _MACH_PORT_MSCOUNT_T_
typedef natural_t mach_port_mscount_t;
#endif
#ifndef _MACH_PORT_MSGCOUNT_T_
#define _MACH_PORT_MSGCOUNT_T_
typedef natural_t mach_port_msgcount_t;
#endif
#ifndef _MACH_PORT_RIGHTS_T_
#define _MACH_PORT_RIGHTS_T_
typedef natural_t mach_port_rights_t;
#endif


/*
 * mach_port_qos_t — required by the generated mach_port MIG client
 * (mach_port_allocate_qos / mach_port_allocate_full). Mirrors the kernel's
 * sys/sys/mach/port.h:378 field-for-field.
 */
#ifndef _MACH_PORT_QOS_T_DEFINED_
#define _MACH_PORT_QOS_T_DEFINED_
typedef struct mach_port_qos {
	unsigned int	name:1;		/* name given */
	unsigned int	prealloc:1;	/* prealloced message */
	boolean_t	pad1:30;
	natural_t	len;
} mach_port_qos_t;
#endif /* _MACH_PORT_QOS_T_DEFINED_ */

#endif /* !_MACH_PORT_H_ */
