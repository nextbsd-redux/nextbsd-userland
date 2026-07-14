/*
 * wpa_ctrl.h — a minimal client for wpa_supplicant's control socket.
 *
 * wland drives STOCK wpa_supplicant, from FreeBSD base, completely unmodified.
 * That is a deliberate and load-bearing decision (WLAN plan §3.3):
 *
 *   * wpa_supplicant is the one component in the network stack where "three
 *     months behind upstream" is an exploit. Stock, a security fix is a base
 *     rebuild. Vendored-and-patched, every upstream release becomes a merge
 *     against our Mach patches, forever.
 *   * It already speaks net80211 through driver_bsd.c. It is definitionally the
 *     FreeBSD thing, and nextbsd-userland's own README scopes this repo to "the
 *     system layer that has NO FreeBSD equivalent".
 *   * Teaching it Mach would couple the most security-sensitive, most-frequently
 *     -updated daemon in the tree to the most bespoke, least-settled component
 *     (the mach.ko syscall-slot shim).
 *
 * So wpa_supplicant never links CoreFoundation, never speaks Mach, and does not
 * know configd exists. It talks net80211 ioctls downward and this UNIX control
 * socket upward. ALL the Mach knowledge lives in wland, which gets it for free
 * by linking libSystemConfiguration — exactly how ipconfigd does it.
 *
 * The protocol is plain text over a UNIX DATAGRAM socket at
 * /var/run/wpa_supplicant/<ifname>. The client binds its own socket to a unique
 * path so replies can be routed back. Two connections are used, which is the
 * standard idiom:
 *
 *   command socket  — request/reply. Send "SCAN", read "OK".
 *   event socket    — after sending "ATTACH", wpa_supplicant pushes unsolicited
 *                     event lines ("<3>CTRL-EVENT-CONNECTED ...") down it.
 *
 * They must be separate: an event can arrive at any moment, and if the command
 * socket carried both, a reply read would randomly return an event instead.
 */
#ifndef _WLAND_WPA_CTRL_H_
#define _WLAND_WPA_CTRL_H_

#include <stdbool.h>
#include <stddef.h>

struct wpa_ctrl;

/*
 * Open a control connection for `ifname`. Returns NULL if wpa_supplicant is not
 * listening (it may not have started yet — the caller should retry).
 */
struct wpa_ctrl	*wpa_ctrl_open(const char *ifname);

/*
 * Open an EVENT connection: same socket, plus an ATTACH so wpa_supplicant
 * starts pushing unsolicited events. Returns NULL on failure.
 */
struct wpa_ctrl	*wpa_ctrl_open_event(const char *ifname);

/* The fd, for a dispatch_source / kqueue read watch on the event connection. */
int	wpa_ctrl_fd(const struct wpa_ctrl *c);

/*
 * Send `cmd` and read the reply into `reply` (NUL-terminated). Returns 0 on
 * success, -1 on error or timeout. `timeout_ms` bounds the wait — a scan can
 * take seconds, but the SCAN *command* itself returns immediately, so no
 * request here should ever need long.
 */
int	wpa_ctrl_request(struct wpa_ctrl *c, const char *cmd, char *reply,
	    size_t reply_sz, int timeout_ms);

/* Convenience: send `cmd` and return true iff the reply is exactly "OK". */
bool	wpa_ctrl_ok(struct wpa_ctrl *c, const char *cmd);

/*
 * Read one pending event line from an event connection (opened with
 * wpa_ctrl_open_event). Non-blocking: returns 0 and an empty buffer when
 * nothing is pending, 1 when a line was read, -1 on error.
 *
 * The leading "<N>" priority prefix is stripped, so the caller sees
 * "CTRL-EVENT-CONNECTED - Connection to aa:bb:.. completed".
 */
int	wpa_ctrl_recv_event(struct wpa_ctrl *c, char *buf, size_t buf_sz);

void	wpa_ctrl_close(struct wpa_ctrl *c);

#endif /* _WLAND_WPA_CTRL_H_ */
