# dns-sd — Apple's DNS Service Discovery command-line client.
#
# Vendors upstream Clients/dns-sd.c + ClientCommon.c (the Clients/ subdir was
# deferred in iter 2; see ../VENDORED.md). This is the reference CLI for the
# DNS-SD API: browse (-B), resolve (-L), register a test service (-R),
# getaddrinfo (-G), enumerate types (-B _services._dns-sd._udp), etc.
#
# The tool links the libdns_sd client shared library (../libdns_sd), whose
# stubs speak the Apple AF_UNIX wire protocol to the daemon listening on
# /var/run/mDNSResponder. (dns-sd.c can also compile the stubs in directly via
# -DTEST_NEW_CLIENTSTUB, but we link the already-built libdns_sd instead, the
# same library every other DNS-SD consumer on the system uses.)
#
# Installed as /usr/bin/dns-sd with the upstream man page.

PROG=		dns-sd
MAN=		dns-sd.1

SRCS=		dns-sd.c ClientCommon.c

# dns-sd.1 lives in mDNSShared alongside the other shared man pages.
.PATH:		${.CURDIR}/../mDNSShared

# WARNS=0: vendored Apple source isn't WARNS=6-clean (same trade the daemon's
# and libdns_sd's Makefiles make).
WARNS=		0

CFLAGS+=	-O2 -pipe -fwrapv
CFLAGS+=	-DHAVE_IPV6
CFLAGS+=	-D_THREAD_SAFE
CFLAGS+=	-I${.CURDIR}
CFLAGS+=	-I${.CURDIR}/../mDNSShared

# Link libdns_sd, installed to ${PREFIX}/lib/system by ../libdns_sd. Search the
# staged install dir first (DESTDIR base build), then the live path (for an
# incremental build on an already-installed system).
LDFLAGS+=	-L${DESTDIR}${PREFIX}/lib/system -L${PREFIX}/lib/system
LDADD+=		-ldns_sd

BINDIR=		${PREFIX}/bin

PREFIX?=	/usr

.include <bsd.prog.mk>
