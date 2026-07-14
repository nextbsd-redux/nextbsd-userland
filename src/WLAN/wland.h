/*
 * wland.h — wland's shared interface table.
 *
 * One entry per 802.11 radio: the PHY, the VAP we cloned off it, the
 * wpa_supplicant we spawned for that VAP, and our control connections to it.
 *
 * Two threads touch this: the main event loop (which drives wpa_supplicant and
 * publishes to configd) and the Mach service thread (which answers wlan.defs
 * RPCs from the `wlan` CLI). A single mutex guards the table — the access rate
 * is a handful of events per minute, so anything finer-grained would be
 * complexity for nothing.
 */
#ifndef _WLAND_H_
#define _WLAND_H_

#include <net/if.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/types.h>

#include "supplicant.h"
#include "vap.h"

struct wlan_if {
	bool			active;
	char			phy[IF_NAMESIZE];	/* iwlwifi0 */
	char			vap[IF_NAMESIZE];	/* wlan0 */
	pid_t			supplicant_pid;		/* our wpa_supplicant */
	struct supplicant	*supp;			/* NULL until attached */
};

/* Daemon shutdown flag, set by the signal handler. */
extern volatile sig_atomic_t	wland_got_term;

/*
 * Lock / unlock the interface table. The MIG routine bodies in wland_mig.c call
 * these directly — they run on the Mach service thread, and every field they
 * read is written by the main loop.
 */
void	wland_lock(void);
void	wland_unlock(void);

/* Number of table slots (bounded by WLAN_MAX_PHY). Caller holds the lock. */
int	wland_if_count_locked(void);

/* The idx-th active interface, or NULL. Caller holds the lock. */
struct wlan_if	*wland_if_at_locked(int idx);

/* Look an interface up by VAP name ("wlan0"), or NULL. Caller holds the lock. */
struct wlan_if	*wland_if_by_name_locked(const char *vap);

#endif /* _WLAND_H_ */
