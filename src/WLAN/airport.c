/*
 * airport.c — wland's SystemConfiguration surface. See airport.h.
 *
 * Single CoreFoundation translation unit, deliberately: the rest of wland stays
 * plain C with plain BSD headers, exactly as ipconfigd keeps CF confined to
 * sc_publish.c.
 *
 * NOTE ON OBJECTIVE-C: there is none here, and there cannot be. There is no
 * libobjc2 and no Foundation in nextbsd-userland — zero .m files are built and
 * the toolchain enables C/C++ only. The CoreWLAN-shaped ObjC API belongs ABOVE
 * the Mach line, in Gershwin, reached through the `wlan` CLI. That is not a
 * limitation we are working around; it is where the API belongs architecturally.
 */
#include "airport.h"

#include <SystemConfiguration/SCDynamicStore.h>
#include <SystemConfiguration/SCPreferences.h>
#include <CoreFoundation/CoreFoundation.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/*
 * Our SCPreferences application ID. Files land under
 * /Local/Library/Preferences/SystemConfiguration/.
 */
#define	WLAND_PREFS_APP		"org.nextbsd.wland"
#define	KNOWN_NETWORKS_KEY	"KnownNetworks"

struct airport {
	SCDynamicStoreRef	store;
};

static void
xlog(const char *fmt, ...)
{
	va_list ap;

	(void)fprintf(stderr, "wland[sc] ");
	va_start(ap, fmt);
	(void)vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void)fputc('\n', stderr);
	(void)fflush(stderr);
}

/* CFStringCreateWithCString wrapper — avoids depending on -fconstant-cfstrings. */
static CFStringRef
mkstr(const char *s)
{
	return (CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8));
}

static CFNumberRef
mknum(int v)
{
	return (CFNumberCreate(NULL, kCFNumberIntType, &v));
}

static const char *
security_name(wlan_security_t s)
{
	switch (s) {
	case wlan_security_open_e:		return ("None");
	case wlan_security_wep_e:		return ("WEP");
	case wlan_security_wpa_e:		return ("WPA");
	case wlan_security_wpa2_e:		return ("WPA2");
	case wlan_security_wpa3_e:		return ("WPA3");
	case wlan_security_enterprise_e:	return ("Enterprise");
	default:				return ("Unknown");
	}
}

struct airport *
airport_open(void)
{
	struct airport *a;
	CFStringRef name;

	a = calloc(1, sizeof(*a));
	if (a == NULL)
		return (NULL);

	name = mkstr("wland");
	a->store = SCDynamicStoreCreate(NULL, name, NULL, NULL);
	if (name != NULL)
		CFRelease(name);
	if (a->store == NULL) {
		/*
		 * configd may not be up yet. There is no launchd service
		 * ordering in this port — no StartAfter, no Requires, and the
		 * plist directory scan is not even sorted — so "start after
		 * configd" is expressed as KeepAlive plus tolerating NULL here
		 * and opening lazily. Never assume configd exists in main().
		 */
		free(a);
		return (NULL);
	}
	return (a);
}

void
airport_close(struct airport *a)
{
	if (a == NULL)
		return;
	if (a->store != NULL)
		CFRelease(a->store);
	free(a);
}

static CFStringRef
airport_key(const char *ifname)
{
	CFStringRef cf_if, key;

	cf_if = mkstr(ifname);
	if (cf_if == NULL)
		return (NULL);
	key = CFStringCreateWithFormat(NULL, NULL,
	    CFSTR("State:/Network/Interface/%@/AirPort"), cf_if);
	CFRelease(cf_if);
	return (key);
}

int
airport_publish(struct airport *a, const char *ifname,
    const struct assoc_state *st)
{
	CFMutableDictionaryRef d;
	CFStringRef key, s;
	CFNumberRef n;
	Boolean ok;

	if (a == NULL || a->store == NULL || ifname == NULL || st == NULL)
		return (-1);

	d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks,
	    &kCFTypeDictionaryValueCallBacks);
	if (d == NULL)
		return (-1);

	/*
	 * Authenticated is the load-bearing field — it is what ipconfigd's DHCP
	 * gate reads. It is true ONLY in the connected state, i.e. only after
	 * CTRL-EVENT-CONNECTED, which is only after the 4-way handshake. Setting
	 * it true at association would reintroduce precisely the bug the gate
	 * exists to prevent: DHCP onto a port that is not yet keyed.
	 */
	CFDictionarySetValue(d, CFSTR("Authenticated"),
	    st->state == wlan_state_connected_e ? kCFBooleanTrue
	                                        : kCFBooleanFalse);

	if (st->ssid[0] != '\0') {
		s = mkstr(st->ssid);
		if (s != NULL) {
			CFDictionarySetValue(d, CFSTR("SSID"), s);
			CFRelease(s);
		}
	}
	if (st->bssid[0] != '\0') {
		s = mkstr(st->bssid);
		if (s != NULL) {
			CFDictionarySetValue(d, CFSTR("BSSID"), s);
			CFRelease(s);
		}
	}

	n = mknum(st->rssi);
	if (n != NULL) {
		CFDictionarySetValue(d, CFSTR("RSSI"), n);
		CFRelease(n);
	}
	n = mknum(st->channel);
	if (n != NULL) {
		CFDictionarySetValue(d, CFSTR("Channel"), n);
		CFRelease(n);
	}
	s = mkstr(security_name(st->security));
	if (s != NULL) {
		CFDictionarySetValue(d, CFSTR("Security"), s);
		CFRelease(s);
	}

	key = airport_key(ifname);
	if (key == NULL) {
		CFRelease(d);
		return (-1);
	}
	ok = SCDynamicStoreSetValue(a->store, key, d);
	CFRelease(key);
	CFRelease(d);

	if (!ok) {
		xlog("WLAN-AIRPORT-FAIL: could not set State:/Network/"
		    "Interface/%s/AirPort", ifname);
		return (-1);
	}

	/*
	 * WLAN-AIRPORT-OK is the marker that says the association-complete
	 * signal reached the store — the one thing ipconfigd's gate depends on.
	 */
	xlog("WLAN-AIRPORT-OK: %s Authenticated=%s SSID='%s' RSSI=%d ch=%d %s",
	    ifname,
	    st->state == wlan_state_connected_e ? "true" : "false",
	    st->ssid, st->rssi, st->channel, security_name(st->security));
	return (0);
}

int
airport_remove(struct airport *a, const char *ifname)
{
	CFStringRef key;

	if (a == NULL || a->store == NULL)
		return (-1);
	key = airport_key(ifname);
	if (key == NULL)
		return (-1);
	(void)SCDynamicStoreRemoveValue(a->store, key);
	CFRelease(key);
	xlog("removed State:/Network/Interface/%s/AirPort", ifname);
	return (0);
}

/* ------------------------------------------------------------------ */
/* Known networks (SCPreferences)                                       */
/* ------------------------------------------------------------------ */

/*
 * Open the prefs, hand back the KnownNetworks dict (mutable copy), or an empty
 * one. Caller releases both.
 */
static SCPreferencesRef
known_open(CFMutableDictionaryRef *nets_out)
{
	SCPreferencesRef prefs;
	CFDictionaryRef existing;
	CFStringRef app, key;

	app = mkstr(WLAND_PREFS_APP);
	if (app == NULL)
		return (NULL);
	prefs = SCPreferencesCreate(NULL, app, NULL);
	CFRelease(app);
	if (prefs == NULL)
		return (NULL);

	key = mkstr(KNOWN_NETWORKS_KEY);
	if (key == NULL) {
		CFRelease(prefs);
		return (NULL);
	}
	existing = SCPreferencesGetValue(prefs, key);
	CFRelease(key);

	if (existing != NULL &&
	    CFGetTypeID(existing) == CFDictionaryGetTypeID()) {
		*nets_out = CFDictionaryCreateMutableCopy(NULL, 0, existing);
	} else {
		*nets_out = CFDictionaryCreateMutable(NULL, 0,
		    &kCFTypeDictionaryKeyCallBacks,
		    &kCFTypeDictionaryValueCallBacks);
	}
	if (*nets_out == NULL) {
		CFRelease(prefs);
		return (NULL);
	}
	return (prefs);
}

/* Write KnownNetworks back and commit. SCPreferences does temp-file + rename. */
static int
known_commit(SCPreferencesRef prefs, CFMutableDictionaryRef nets)
{
	CFStringRef key;
	int rc = -1;

	key = mkstr(KNOWN_NETWORKS_KEY);
	if (key == NULL)
		return (-1);
	if (SCPreferencesSetValue(prefs, key, nets) &&
	    SCPreferencesCommitChanges(prefs))
		rc = 0;
	else
		xlog("SCPreferences commit failed — known networks not saved");
	CFRelease(key);
	return (rc);
}

int
known_net_save(const char *ssid, const char *psk, bool autojoin)
{
	SCPreferencesRef prefs;
	CFMutableDictionaryRef nets, entry;
	CFStringRef cf_ssid, s;
	CFNumberRef n;
	int now, rc;

	if (ssid == NULL || ssid[0] == '\0')
		return (-1);

	prefs = known_open(&nets);
	if (prefs == NULL)
		return (-1);

	entry = CFDictionaryCreateMutable(NULL, 0,
	    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (entry == NULL) {
		CFRelease(nets);
		CFRelease(prefs);
		return (-1);
	}

	/*
	 * Plaintext PSK. Unavoidable — there is no keychain (see airport.h).
	 * The file is root-owned; SCPreferences writes it 0644 by default, so
	 * wland's caller chmods the prefs directory. When a keychain lands, this
	 * is the line that changes.
	 */
	if (psk != NULL && psk[0] != '\0') {
		s = mkstr(psk);
		if (s != NULL) {
			CFDictionarySetValue(entry, CFSTR("PSK"), s);
			CFRelease(s);
		}
	}
	CFDictionarySetValue(entry, CFSTR("AutoJoin"),
	    autojoin ? kCFBooleanTrue : kCFBooleanFalse);

	now = (int)time(NULL);
	n = mknum(now);
	if (n != NULL) {
		CFDictionarySetValue(entry, CFSTR("LastUsed"), n);
		CFRelease(n);
	}

	cf_ssid = mkstr(ssid);
	if (cf_ssid != NULL) {
		CFDictionarySetValue(nets, cf_ssid, entry);
		CFRelease(cf_ssid);
	}
	CFRelease(entry);

	rc = known_commit(prefs, nets);
	if (rc == 0)
		xlog("remembered network '%s' (autojoin=%s)", ssid,
		    autojoin ? "yes" : "no");

	CFRelease(nets);
	CFRelease(prefs);
	return (rc);
}

bool
known_net_find(const char *ssid, struct known_net *out)
{
	SCPreferencesRef prefs;
	CFMutableDictionaryRef nets;
	CFDictionaryRef entry;
	CFStringRef cf_ssid, psk;
	CFBooleanRef aj;
	bool found = false;

	if (ssid == NULL || out == NULL)
		return (false);

	prefs = known_open(&nets);
	if (prefs == NULL)
		return (false);

	cf_ssid = mkstr(ssid);
	if (cf_ssid != NULL) {
		entry = CFDictionaryGetValue(nets, cf_ssid);
		if (entry != NULL &&
		    CFGetTypeID(entry) == CFDictionaryGetTypeID()) {
			(void)memset(out, 0, sizeof(*out));
			(void)strlcpy(out->ssid, ssid, sizeof(out->ssid));

			psk = CFDictionaryGetValue(entry, CFSTR("PSK"));
			if (psk != NULL &&
			    CFGetTypeID(psk) == CFStringGetTypeID())
				(void)CFStringGetCString(psk, out->psk,
				    sizeof(out->psk), kCFStringEncodingUTF8);

			aj = CFDictionaryGetValue(entry, CFSTR("AutoJoin"));
			out->autojoin = (aj == NULL ||
			    CFGetTypeID(aj) != CFBooleanGetTypeID() ||
			    CFBooleanGetValue(aj));
			found = true;
		}
		CFRelease(cf_ssid);
	}

	CFRelease(nets);
	CFRelease(prefs);
	return (found);
}

int
known_net_forget(const char *ssid)
{
	SCPreferencesRef prefs;
	CFMutableDictionaryRef nets;
	CFStringRef cf_ssid;
	int rc = -1;

	if (ssid == NULL)
		return (-1);
	prefs = known_open(&nets);
	if (prefs == NULL)
		return (-1);

	cf_ssid = mkstr(ssid);
	if (cf_ssid != NULL) {
		CFDictionaryRemoveValue(nets, cf_ssid);
		CFRelease(cf_ssid);
		rc = known_commit(prefs, nets);
		if (rc == 0)
			xlog("forgot network '%s'", ssid);
	}

	CFRelease(nets);
	CFRelease(prefs);
	return (rc);
}

bool
known_net_pick(const struct scan_entry *scan, int scan_n, struct known_net *out)
{
	int i, best = -1, best_rssi = -1000;

	if (scan == NULL || out == NULL || scan_n <= 0)
		return (false);

	/*
	 * The autojoin brain wpa_supplicant does not have: pick the strongest
	 * remembered, autojoin-enabled network that is actually in range.
	 *
	 * wpa_supplicant only offers a `priority` integer, which cannot express
	 * "prefer whichever of my known networks I can actually hear best right
	 * now" — and iwd, which does have a real autojoin model, is unusable
	 * here (welded to nl80211, and LGPL). So this lives in wland.
	 */
	for (i = 0; i < scan_n; i++) {
		struct known_net kn;

		if (scan[i].ssid[0] == '\0')
			continue;
		if (!known_net_find(scan[i].ssid, &kn))
			continue;
		if (!kn.autojoin)
			continue;
		if (scan[i].rssi > best_rssi) {
			best_rssi = scan[i].rssi;
			best = i;
		}
	}
	if (best < 0)
		return (false);

	if (!known_net_find(scan[best].ssid, out))
		return (false);
	xlog("autojoin: '%s' is the strongest known network in range "
	    "(RSSI %d dBm)", out->ssid, best_rssi);
	return (true);
}
