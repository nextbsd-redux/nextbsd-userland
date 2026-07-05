PROG=	kextstat
SRCS=	kextstat.c

# Man page vendored from apple-oss-distributions/kext_tools @ kext_tools-779
# (kextstat.8). bsd.prog.mk gzips + installs into MANDIR.
MAN=	kextstat.8

# Pure kld enumeration — kldnext(2)/kldstat(2) live in libc, no CF needed.

.include <bsd.prog.mk>
