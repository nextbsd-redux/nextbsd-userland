/*
 * mach/notify.h — Mach port-notification message vocabulary.
 *
 * Defines the MACH_NOTIFY_* message IDs and the notification message
 * struct layouts (port-deleted / port-destroyed / no-senders /
 * send-once / dead-name). MIG-generated headers pull this in: the
 * launchd job interface's job.h includes <mach/notify.h> because the
 * server demux needs to recognize notification messages.
 *
 * Adapted from ravynOS sys/sys/mach/notify.h — kernel-side
 * <sys/mach/...> includes rewritten to our flattened userland <mach/>
 * layout, and the kernel-only <vm/vm.h> (pulled there just for
 * boolean_t) replaced with <mach/boolean.h>.
 *
 * NB: our mach.ko does not actually *deliver* these notifications yet
 * (dead-name notification is one of the "hard" kernel gaps from the
 * launchd-842 plan). This header is the type vocabulary so launchd's
 * notification-handling code compiles; the messages just never arrive
 * until the kernel side lands.
 */
#ifndef _MACH_NOTIFY_H_
#define _MACH_NOTIFY_H_

#include <sys/types.h>
#include <mach/boolean.h>
#include <mach/port.h>
#include <mach/message.h>
#include <mach/ndr.h>

/*
 *  An alternative specification of the notification interface
 *  may be found in mach/notify.defs.
 */

#define MACH_NOTIFY_FIRST		0100
#ifndef MACH_NOTIFY_PORT_DELETED
#define MACH_NOTIFY_PORT_DELETED	(MACH_NOTIFY_FIRST + 001 )
#endif
			/* A send or send-once right was deleted. */
#ifndef MACH_NOTIFY_PORT_DESTROYED
#define MACH_NOTIFY_PORT_DESTROYED	(MACH_NOTIFY_FIRST + 005)
#endif
			/* A receive right was (would have been) deallocated */
#ifndef MACH_NOTIFY_NO_SENDERS
#define MACH_NOTIFY_NO_SENDERS		(MACH_NOTIFY_FIRST + 006)
#endif
			/* Receive right has no extant send rights */
#ifndef MACH_NOTIFY_SEND_ONCE
#define MACH_NOTIFY_SEND_ONCE		(MACH_NOTIFY_FIRST + 007)
#endif
			/* An extant send-once right died */
#ifndef MACH_NOTIFY_DEAD_NAME
#define MACH_NOTIFY_DEAD_NAME		(MACH_NOTIFY_FIRST + 010)
#endif
			/* Send or send-once right died, leaving a dead-name */
#ifndef MACH_NOTIFY_LAST
#define MACH_NOTIFY_LAST		(MACH_NOTIFY_FIRST + 015)
#endif

typedef struct {
    mach_msg_header_t	not_header;
    NDR_record_t	NDR;
    mach_port_name_t not_port;/* MACH_MSG_TYPE_PORT_NAME */
    mach_msg_format_0_trailer_t trailer;
} mach_port_deleted_notification_t;

typedef struct {
    mach_msg_header_t	not_header;
    mach_msg_body_t	not_body;
    mach_msg_port_descriptor_t not_port;/* MACH_MSG_TYPE_PORT_RECEIVE */
    mach_msg_format_0_trailer_t trailer;
} mach_port_destroyed_notification_t;

typedef struct {
    mach_msg_header_t	not_header;
    NDR_record_t	NDR;
    mach_msg_type_number_t not_count;
    mach_msg_format_0_trailer_t trailer;
} mach_no_senders_notification_t;

typedef struct {
    mach_msg_header_t	not_header;
    mach_msg_format_0_trailer_t trailer;
} mach_send_once_notification_t;

typedef struct {
    mach_msg_header_t	not_header;
    NDR_record_t	NDR;
    mach_port_name_t not_port;/* MACH_MSG_TYPE_PORT_NAME */
    mach_msg_format_0_trailer_t trailer;
} mach_dead_name_notification_t;

#endif	/* _MACH_NOTIFY_H_ */
