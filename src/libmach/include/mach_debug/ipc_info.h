/*
 * mach_debug/ipc_info.h — userland mirror of the kernel's
 * sys/sys/mach_debug/ipc_info.h.
 *
 * The generated mach_port MIG client declares mach_port_space_info() and
 * mach_port_space_basic_info(), so these types have to exist on the userland
 * side even though libmach itself calls neither. Kept in the same header and
 * with the same field names as the kernel copy so the two stay comparable;
 * layouts are byte-identical (all natural_t), which is what the wire needs.
 *
 * These are also the types mach_debug_types.defs describes — see
 * nextbsd-kernel src-overlay/sys/sys/mach_debug/.
 */

#ifndef _MACH_DEBUG_IPC_INFO_H_
#define _MACH_DEBUG_IPC_INFO_H_

#include <mach/std_types.h>
#include <mach/port.h>

typedef struct ipc_info_space {
	natural_t iis_genno_mask;	/* generation number mask */
	natural_t iis_table_size;	/* size of table */
	natural_t iis_table_next;	/* next possible size of table */
	natural_t iis_tree_size;	/* size of tree (UNUSED) */
	natural_t iis_tree_small;	/* # of small entries in tree (UNUSED) */
	natural_t iis_tree_hash;	/* # of hashed entries in tree (UNUSED) */
} ipc_info_space_t;

typedef struct ipc_info_space_basic {
	natural_t iisb_genno_mask;	/* generation number mask */
	natural_t iisb_table_size;	/* size of table */
	natural_t iisb_table_next;	/* next possible size of table */
	natural_t iisb_table_inuse;	/* number of entries in use */
	natural_t iisb_reserved[2];	/* future expansion */
} ipc_info_space_basic_t;

typedef struct ipc_info_name {
	mach_port_name_t iin_name;	/* port name, including gen number */
	integer_t	 iin_collision;	/* collision at this entry? */
	mach_port_type_t iin_type;	/* straight port type */
	mach_port_urefs_t iin_urefs;	/* user-references */
	natural_t	 iin_object;	/* object pointer/identifier */
	natural_t	 iin_next;	/* marequest/next in free list */
	natural_t	 iin_hash;	/* hash index */
} ipc_info_name_t;

typedef ipc_info_name_t *ipc_info_name_array_t;

/* UNUSED */
typedef struct ipc_info_tree_name {
	ipc_info_name_t	 iitn_name;
	mach_port_name_t iitn_lchild;	/* name of left child */
	mach_port_name_t iitn_rchild;	/* name of right child */
} ipc_info_tree_name_t;

typedef ipc_info_tree_name_t *ipc_info_tree_name_array_t;

#endif	/* _MACH_DEBUG_IPC_INFO_H_ */
