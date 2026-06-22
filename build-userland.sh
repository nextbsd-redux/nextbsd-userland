#!/usr/bin/env bash
# =============================================================================
# build-userland.sh — cross-build driver for the NextBSD Apple SYSTEM LAYER
# =============================================================================
#
# SCOPE
#   Cross-compiles ONLY Tiers 0-2 of the Apple userland — the "system layer":
#   the host MIG tool, the libsystem_* core libraries, launchd/launchctl, and
#   the first-tier system daemons (configd, IOKit facade, kext_tools, notify,
#   ASL/syslog, IPConfiguration, mDNSResponder, DiskArbitration, hostnamed),
#   for ONE architecture, into $DESTDIR (/stage), in strict dependency order.
#
# INTENTIONALLY EXCLUDED (do NOT add here)
#   * Command suites / the broad userland — those come from FreeBSD base and
#     are curated via compat's srclist, not built in this repo.
#   * PAM (OpenPAM port, build.sh step 3y2 ~2609+) — a later tier; it overlays
#     FreeBSD-pam and needs its own conflict handling. Not a Tier 0-2 concern.
#   * Every smoke/round-trip TEST binary build.sh compiles next to each
#     component (test_libmach, configtest, iokittest, sctest, datest, ...).
#     Those exec just-built TARGET binaries on the host — impossible when
#     cross-building. The runtime assertions they provided MOVE to the boot
#     test (CI runs them inside the booted VM, not here).
#
# EXECUTION CONTEXT (set up by CI BEFORE this script runs)
#   Running inside the nextbsd-kernel-toolchain container on a GitHub Ubuntu
#   (x86_64) runner. CI has already:
#     1. staged a SYSROOT from compat's continuous base into $SYSROOT, and
#     2. run, in the freebsd src tree:
#          cd /usr/src && ./tools/build/make.py \
#              --cross-bindir=$CROSS_BINDIR TARGET=$T TARGET_ARCH=$TA \
#              toolchain _includes
#        so the cross clang/lld exist in $CROSS_BINDIR and the staged
#        sysroot carries the FreeBSD base headers.
#
# HOW EACH COMPONENT TYPE IS BUILT (cross-shaping, vs build.sh's host+chroot)
#   * bsd.lib.mk / bsd.prog.mk components (the majority): build.sh ran these
#     as `make -C src/X DESTDIR=$WORK/rootfs ...` on a FreeBSD host. Here they
#     must compile for $TA, so we drive them THROUGH the FreeBSD cross build
#     env, which sets MACHINE_ARCH, the cross clang, and the sysroot
#     consistently:
#         run_buildenv "make -C $ROOT/src/X DESTDIR=$DESTDIR SYSROOT=$SYSROOT ..."
#     via `make.py ... buildenv BUILDENV_SHELL=<script>`. We pass the same
#     DESTDIR / PREFIX / SYSROOT / MIGOUT variables build.sh passed, verbatim,
#     so the component Makefiles behave identically — only the compiler/arch
#     underneath changes.
#   * The 2 CMake components (libdispatch, swift-foundation-icu) are HOST
#     cmake/ninja runs that emit TARGET objects via -DCMAKE_TOOLCHAIN_FILE.
#     No chroot; no `make.py buildenv`.
#   * migcom (Tier 0) is a HOST tool — built with the runner's plain `cc`,
#     NOT cross. Its codegen output is arch-neutral, so one host migcom serves
#     every target's MIG steps.
#
# WHAT WAS DROPPED FROM build.sh's blocks (per cross-shaping rules)
#   * Every `chroot $WORK/rootfs ldconfig -m/-r` hint-prime and every
#     `chroot ... ldd ... | grep` resolution check. We can't chroot into a
#     foreign-arch rootfs on the x86 runner. Where a resolution assertion was
#     load-bearing we replace it with a cross `readelf -d` DT_NEEDED check
#     (see needed_check); elsewhere we omit it (rtld resolution is re-verified
#     in the boot test).
#   * Build-time runs of just-built TARGET binaries (test_corefoundation
#     ~1429, the kextdeps dependency smoke ~2005). Can't exec cross binaries.
#
# IDEMPOTENCE
#   Re-runnable: mkdir -p is harmless, installs overwrite, MIG out dirs are
#   recreated. Not a hermetic clean — CI gives a fresh checkout per run.
#
# STATUS
#   v0. Expected to need CI iteration — several blocks copy build.sh flags
#   verbatim and carry `# TODO verify under cross` where behaviour under the
#   cross build env (vs build.sh's native host build) is unproven.
# =============================================================================

set -euo pipefail

# ---- required environment ----------------------------------------------------
: "${T:?set T to the target (e.g. amd64 / arm64)}"
: "${TA:?set TA to the target_arch (e.g. amd64 / aarch64)}"
: "${SYSROOT:?set SYSROOT to the staged sysroot}"
: "${DESTDIR:=/stage}"
: "${CROSS_BINDIR:?set CROSS_BINDIR to the dir holding the cross clang/lld}"

# Root of the freebsd src tree make.py lives in (CI's `cd /usr/src`).
: "${SRCTREE:=/usr/src}"

# Repo root — this script lives at the top of nextbsd-userland.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/src"

# Per-target CMake cross toolchain file (we wrote cross-amd64 / cross-arm64).
CMAKE_TOOLCHAIN="$ROOT/cmake/cross-${T}.cmake"

# MIG artifact dirs (pure build artifacts; never installed into $DESTDIR).
MIGROOT="${MIGROOT:-$ROOT/.mig}"

# Host migcom — built by Tier 0 (FreeBSD legacy stage) and used build-only by
# every MIG step. It is a runner-native (x86_64-Linux) binary, so it is NOT
# shipped; Tier 0 points MIGCOM at its legacy build location.
# (mig.sh shells out to $MIGCOM; $MIGCC preprocesses .defs — host cc, arch-neutral.)
MIGCOM=""   # set by Tier 0 (legacy-built, build-scratch — never shipped)
MIG_SH="$SRC/bootstrap_cmds/migcom.tproj/mig.sh"
MIGCC="${MIGCC:-cc}"

export SYSROOT DESTDIR CROSS_BINDIR

# ---- logging helpers ---------------------------------------------------------
tier()  { echo; echo "############################################################"; \
          echo "## $*"; echo "############################################################"; }
comp()  { echo; echo "==> [$T/$TA] $*"; }
note()  { echo "    - $*"; }

# ---- run a make/shell command inside the FreeBSD cross build env -------------
# Replaces build.sh's `make -C src/X ... DESTDIR=$WORK/rootfs` (native host)
# and its chroot wrappers. make.py's `buildenv` sets MACHINE/MACHINE_ARCH,
# CC=$CROSS_BINDIR/clang, the cross binutils, and the sysroot so the component
# Makefiles cross-compile without per-component CC plumbing.
run_buildenv() {
    local script="$1"
    ( cd "$SRCTREE" && ./tools/build/make.py \
        --cross-bindir="$CROSS_BINDIR" \
        TARGET="$T" TARGET_ARCH="$TA" \
        buildenv BUILDENV_SHELL="/bin/sh -ex -c '$script'" )
}

# Like run_buildenv but pinned to the RUNNER's native arch (amd64) — for HOST
# tools (migcom) that must execute on the x86_64 runner regardless of the job's
# target. Uses the FreeBSD buildenv (bmake + share/mk) so `.include <bsd.prog.mk>`
# parses; plain `make` on Ubuntu is GNU make and fails ("missing separator").
run_buildenv_host() {
    local script="$1"
    ( cd "$SRCTREE" && ./tools/build/make.py \
        --cross-bindir="$CROSS_BINDIR" \
        TARGET=amd64 TARGET_ARCH=amd64 \
        buildenv BUILDENV_SHELL="/bin/sh -ex -c '$script'" )
}

# ---- cross readelf DT_NEEDED sanity check (replaces chroot ldd assertions) ---
# build.sh asserted resolution with `chroot ... ldd ... | grep`. We can't run a
# foreign-arch ldd on the runner, so verify the just-linked TARGET object
# DECLARES the expected DT_NEEDED instead. Actual rtld resolution is re-checked
# in the boot test. $1 = path under $DESTDIR; $2 = soname substring to require.
needed_check() {
    local obj="$1" want="$2"
    local readelf="$CROSS_BINDIR/readelf"
    [ -x "$readelf" ] || readelf="${CROSS_BINDIR}/llvm-readelf"
    if [ ! -x "$readelf" ]; then
        note "skip DT_NEEDED check ($want): no cross readelf in CROSS_BINDIR  # TODO verify under cross"
        return 0
    fi
    "$readelf" -d "$obj" 2>/dev/null | grep -q "NEEDED.*$want" \
        || { echo "FAIL: $obj missing DT_NEEDED $want"; exit 1; }
    note "DT_NEEDED ok: $(basename "$obj") -> $want"
}

# ---- run one mig.sh invocation (host migcom; arch-neutral codegen) -----------
# Mirrors build.sh's `MIGCC=cc MIGCOM=.../migcom /bin/sh mig.sh ...` pattern.
run_mig() {
    local outdir="$1"; shift
    ( cd "$outdir" && MIGCC="$MIGCC" MIGCOM="$MIGCOM" /bin/sh "$MIG_SH" "$@" )
}

echo "==> nextbsd-userland system-layer cross build"
echo "    T=$T TA=$TA"
echo "    SYSROOT=$SYSROOT"
echo "    DESTDIR=$DESTDIR"
echo "    CROSS_BINDIR=$CROSS_BINDIR"
echo "    CMAKE_TOOLCHAIN=$CMAKE_TOOLCHAIN"
[ -f "$CMAKE_TOOLCHAIN" ] || { echo "FAIL: missing $CMAKE_TOOLCHAIN"; exit 1; }
mkdir -p "$MIGROOT"

# =============================================================================
# TIER 0 — host MIG tool (built for the RUNNER, NOT cross)
# =============================================================================
tier "TIER 0 : migcom + mig wrapper (HOST tool)"

# migcom is a build-time HOST code generator (turns .defs into C). Build it the
# FreeBSD way — the SAME machinery that bootstraps yacc/lex: stage it into the
# in-container /usr/src and build it host-native via the `_legacy` stage with
# LOCAL_LEGACY_DIRS. The legacy loop runs `make obj includes all install
# DESTDIR=${WORLDTMP}/legacy` with BMAKE (the build host's gcc). Ephemeral,
# in-container — like the kernel build patching /usr/src. (`_legacy` is the
# exposed top-level wrapper that sets up WORLDTMP/BMAKE; the baked kernel-toolchain
# already ran it, so this just adds migcom atop the baked tools/build.)
#
# Placement mirrors the install location: migcom installs to /usr/libexec, so its
# source dir lives at /usr/src/libexec/migcom (FreeBSD convention). The Makefile's
# relative -I../../libmach/include then resolves to /usr/src/libmach/include, where
# we drop our libmach headers.
#
# BUILD-ONLY: the result is a runner-native x86_64-Linux binary — it must NOT ship
# on the FreeBSD ISO (it could not run there). So we do NOT install it into
# $DESTDIR; we point MIGCOM at its legacy build location for the MIG steps. `mig`
# is not a base-system runtime dependency — revisit cross-building a TARGET migcom
# only if we ever want `mig` on the installed system.
comp "migcom (legacy stage, host-native, build-only)"
rm -rf /usr/src/libexec/migcom /usr/src/libmach
mkdir -p /usr/src/libexec/migcom /usr/src/libmach
cp -a "$SRC/bootstrap_cmds/migcom.tproj/." /usr/src/libexec/migcom/
cp -a "$SRC/libmach/include" /usr/src/libmach/
( cd "$SRCTREE" && ./tools/build/make.py --cross-bindir="$CROSS_BINDIR" \
    TARGET="$T" TARGET_ARCH="$TA" \
    LOCAL_LEGACY_DIRS=libexec/migcom \
    _legacy )
MIGCOM=$(find /usr/obj -path '*/legacy/usr/libexec/migcom' -type f 2>/dev/null | head -1)
[ -n "$MIGCOM" ] || { echo "FAIL: legacy stage produced no migcom"; exit 1; }
note "host migcom built (build-only, not shipped): $MIGCOM"
# Smoke it — host binary, run directly (no chroot/cross).
MIGCC="$MIGCC" MIGCOM="$MIGCOM" /bin/sh "$MIG_SH" -version \
    || { echo "FAIL: mig -version (host) exited non-zero"; exit 1; }
note "host migcom installed at $MIGCOM"

# =============================================================================
# TIER 1 — core system libraries (cross)
# =============================================================================
tier "TIER 1 : libmach -> libdispatch -> libxpc -> liblaunch -> ICU -> CF"

# ---- libmach (libsystem_kernel) ---------------------------------------------
# build.sh ~742-774. Installs mach/* headers into the sysroot FIRST — every
# later component (-I sysroot) and the CMake HAVE_MACH detection depend on
# them. bsd.lib.mk does NOT auto-create LIBDIR/INCSDIR/FILESDIR, so pre-make
# the /usr/lib/system convention dirs (carried verbatim from build.sh).
#
# NOTE: build.sh DEFERS installing <mach/mach.h> until just before libxpc
# (~1115) so libdispatch's CMake __has_include(<mach/mach.h>) doesn't wrongly
# flip HAVE_MACH. BUT libmach's INCS list (Makefile) includes mach.h, so a
# plain `install` ships it now. We preserve build.sh's ordering by removing
# mach.h from the sysroot right after install and re-copying it before libxpc.
comp "libmach (libsystem_kernel) — installs mach/* headers into sysroot first"
mkdir -p "$DESTDIR/usr/lib/system" \
         "$DESTDIR/usr/include/mach" \
         "$DESTDIR/usr/libdata/pkgconfig"
run_buildenv "make -C $SRC/libmach DESTDIR=$DESTDIR PREFIX=/usr all install"
# Headers also need to be visible in the SYSROOT (not just DESTDIR) for the
# cross compiles of every later component that does -I\$SYSROOT/usr/include.
# DESTDIR==/stage and SYSROOT are distinct; mirror libmach's headers across.
# TODO verify under cross: if CI points SYSROOT at /stage this copy is a
# no-op; if SYSROOT is a separate staged tree it is required.
if [ "$SYSROOT" != "$DESTDIR" ]; then
    mkdir -p "$SYSROOT/usr/include" "$SYSROOT/usr/lib/system" "$SYSROOT/usr/libdata/pkgconfig"
    cp -a "$DESTDIR/usr/include/mach"        "$SYSROOT/usr/include/" 2>/dev/null || true
    cp -a "$DESTDIR/usr/lib/system/."        "$SYSROOT/usr/lib/system/" 2>/dev/null || true
    cp -a "$DESTDIR/usr/libdata/pkgconfig/." "$SYSROOT/usr/libdata/pkgconfig/" 2>/dev/null || true
fi
# Defer <mach/mach.h> exactly as build.sh does (re-copied before libxpc).
rm -f "$SYSROOT/usr/include/mach/mach.h" "$DESTDIR/usr/include/mach/mach.h"
# Apple-convention <servers/bootstrap.h> (build.sh ~874).
mkdir -p "$DESTDIR/usr/include/servers"
cp "$SRC/libmach/include/servers/bootstrap.h" "$DESTDIR/usr/include/servers/bootstrap.h"
[ "$SYSROOT" != "$DESTDIR" ] && { mkdir -p "$SYSROOT/usr/include/servers"; \
    cp "$SRC/libmach/include/servers/bootstrap.h" "$SYSROOT/usr/include/servers/bootstrap.h"; }
# DROPPED: chroot ldconfig hint-prime, test_libmach/test_mach_port/... builds
# (host-exec of cross binaries — moved to boot test).

sync_sysroot() {
    # Mirror freshly-installed libs+headers from DESTDIR into a distinct
    # SYSROOT so the next cross component links/includes against them.
    # Replaces build.sh's per-component `chroot ... ldconfig -m`. No-op when
    # SYSROOT == DESTDIR.
    [ "$SYSROOT" = "$DESTDIR" ] && return 0
    mkdir -p "$SYSROOT/usr/lib/system" "$SYSROOT/usr/include"
    cp -a "$DESTDIR/usr/lib/system/." "$SYSROOT/usr/lib/system/" 2>/dev/null || true
    cp -a "$DESTDIR/usr/include/."    "$SYSROOT/usr/include/"    2>/dev/null || true
}

# ---- libdispatch [CMAKE, host cmake -> target objs] -------------------------
# build.sh ~971-1039. build.sh ran cmake/ninja inside the chroot. Here cmake +
# ninja run on the HOST but cross-compile via -DCMAKE_TOOLCHAIN_FILE. Same
# -D install layout (PREFIX=/usr, LIBDIR=lib/system, header dirs, HAVE_MACH).
comp "libdispatch [cmake cross]"
DISPATCH_BUILD="$ROOT/.build/libdispatch-$T"
rm -rf "$DISPATCH_BUILD"; mkdir -p "$DISPATCH_BUILD"
cmake -G Ninja -S "$SRC/libdispatch" -B "$DISPATCH_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR=lib/system \
    -DINSTALL_DISPATCH_HEADERS_DIR=/usr/include/dispatch \
    -DINSTALL_BLOCK_HEADERS_DIR=/usr/include \
    -DINSTALL_OS_HEADERS_DIR=/usr/include/os \
    -DINSTALL_PRIVATE_HEADERS=ON \
    -DHAVE_MACH:BOOL=ON \
    -DCMAKE_BUILD_TYPE=Release
DESTDIR="$DESTDIR" ninja -C "$DISPATCH_BUILD" install
# build.sh ~1014-1032: CMakeLists sets no SOVERSION, so output is unversioned
# libdispatch.so / libBlocksRuntime.so. Create the .so.0 + Apple-canonical
# libsystem_dispatch / libsystem_blocks symlinks (kept verbatim).
( cd "$DESTDIR/usr/lib/system"
  ln -sf libdispatch.so      libdispatch.so.0
  ln -sf libBlocksRuntime.so libBlocksRuntime.so.0
  ln -sf libdispatch.so      libsystem_dispatch.so
  ln -sf libdispatch.so      libsystem_dispatch.so.0
  ln -sf libBlocksRuntime.so libsystem_blocks.so
  ln -sf libBlocksRuntime.so libsystem_blocks.so.0 )
sync_sysroot
# DROPPED: chroot ldconfig re-prime; test_libdispatch* host-exec builds.

# ---- libxpc -----------------------------------------------------------------
# build.sh ~1094-1164. bsd.lib.mk component; SYSROOT tells its Makefile where
# the in-build rootfs headers/libs are. Pass SYSROOT + DESTDIR + PREFIX exactly
# as build.sh did (no direct CFLAGS/LDFLAGS — bmake would clobber CFLAGS+=).
comp "libxpc"
# build.sh ~1115: install the <mach/mach.h> umbrella NOW (deferred past the
# libdispatch HAVE_MACH detection above).
cp "$SRC/libmach/include/mach/mach.h" "$DESTDIR/usr/include/mach/mach.h"
[ "$SYSROOT" != "$DESTDIR" ] && cp "$SRC/libmach/include/mach/mach.h" "$SYSROOT/usr/include/mach/mach.h"
# build.sh ~1127-1134: Apple-shim public headers consumers need from /usr/include.
mkdir -p "$DESTDIR/usr/include/uuid"
cp "$SRC/libxpc/uuid/uuid.h"   "$DESTDIR/usr/include/uuid/uuid.h"
cp "$SRC/libxpc/Availability.h" "$DESTDIR/usr/include/Availability.h"
cp "$SRC/libxpc/launch.h"       "$DESTDIR/usr/include/launch.h"
mkdir -p "$DESTDIR/usr/include/xpc"
run_buildenv "make -C $SRC/libxpc DESTDIR=$DESTDIR PREFIX=/usr SYSROOT=$SYSROOT all install"
sync_sysroot
# build.sh ~1156-1158: shape asserts. .so.4 is the lib, .so is the dev symlink.
test -f "$DESTDIR/usr/lib/system/libxpc.so.4" || { echo "FAIL: libxpc.so.4 missing"; exit 1; }
test -L "$DESTDIR/usr/lib/system/libxpc.so"   || { echo "FAIL: libxpc.so symlink missing"; exit 1; }
# DROPPED: chroot ldconfig hint + ldd. libxpc links --allow-shlib-undefined for
# bootstrap_* (resolved by liblaunch at runtime) — no DT_NEEDED to assert here.

# ---- launchd MIG stubs + liblaunch ------------------------------------------
# build.sh ~1192-1268. Phase I1a: run host mig over launchd's .defs; Phase I1b:
# build liblaunch (bsd.lib.mk) against those stubs via MIGOUT.
comp "launchd MIG stubs (host mig)"
MIG_OUT="$MIGROOT/launchd"
rm -rf "$MIG_OUT"; mkdir -p "$MIG_OUT"
MIG_INCS="-I$SRC/libmach/include -I$SRC/launchd/liblaunch -I$SRC/launchd/src"
for d in job job_forward job_reply internal helper mach_exc notify; do
    note "mig: ${d}.defs"
    run_mig "$MIG_OUT" $MIG_INCS \
        -header "${d}.h" -user "${d}User.c" \
        -server "${d}Server.c" -sheader "${d}Server.h" \
        "$SRC/launchd/src/${d}.defs" \
        || { echo "FAIL: mig could not process ${d}.defs"; exit 1; }
done
for f in job.h jobUser.c jobServer.c; do
    test -s "$MIG_OUT/$f" || { echo "FAIL: mig produced no $f from job.defs"; exit 1; }
done

comp "liblaunch"
run_buildenv "make -C $SRC/launchd/liblaunch DESTDIR=$DESTDIR PREFIX=/usr MIGOUT=$MIG_OUT SYSROOT=$SYSROOT all install"
sync_sysroot
# DROPPED: chroot ldconfig re-prime.

# ---- swift-foundation-icu [CMAKE] -------------------------------------------
# build.sh ~1299-1377. Second CMake component; host cmake/ninja -> target objs.
# Three vendored CMakeLists patches (drop Swift lang, U_DISABLE_RENAMING=1,
# standard install dirs) live in the source tree — no extra -D here.
comp "swift-foundation-icu [cmake cross]"
ICU_BUILD="$ROOT/.build/icu-$T"
rm -rf "$ICU_BUILD"; mkdir -p "$ICU_BUILD"
cmake -G Ninja -S "$SRC/swift-foundation-icu" -B "$ICU_BUILD" \
    -DCMAKE_TOOLCHAIN_FILE="$CMAKE_TOOLCHAIN" \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INSTALL_LIBDIR=lib/system \
    -DCMAKE_INSTALL_INCLUDEDIR=include \
    -DCMAKE_BUILD_TYPE=Release
DESTDIR="$DESTDIR" ninja -C "$ICU_BUILD" install
# build.sh ~1350-1352: libicucore.so alias for the _FoundationICU target.
( cd "$DESTDIR/usr/lib/system"
  ln -sf lib_FoundationICU.so libicucore.so
  ln -sf lib_FoundationICU.so libicucore.so.74
  ln -sf lib_FoundationICU.so lib_FoundationICU.so.74 )
sync_sysroot

# ---- libCoreFoundation ------------------------------------------------------
# build.sh ~1379-1402. bsd.lib.mk; pre-create the CoreFoundation include dir
# (bsd.incs.mk doesn't). DEPLOYMENT_RUNTIME_SWIFT=0 + the ICU-using SRCS drop
# are baked into the Makefile, so the invocation is the plain DESTDIR/SYSROOT one.
comp "libCoreFoundation"
mkdir -p "$DESTDIR/usr/include/CoreFoundation"
run_buildenv "make -C $SRC/libCoreFoundation DESTDIR=$DESTDIR PREFIX=/usr SYSROOT=$SYSROOT all install"
sync_sysroot
test -f "$DESTDIR/usr/lib/system/libCoreFoundation.so.6" \
    || { echo "FAIL: libCoreFoundation.so.6 missing"; exit 1; }
# RELOCATED (build.sh ~1410-1431): test_corefoundation was BUILT and then RUN
# under chroot to prove CF initializes (CF resolves _CFGetCurrentDirectory /
# _CFThreadSetName eagerly at library init). We can't exec a cross binary on
# the x86 runner, so the CF-init runtime assertion MOVES to the boot test.

# =============================================================================
# TIER 2 — system daemons + facades (cross)
# =============================================================================
tier "TIER 2 : launchd -> configd -> SC -> IOKit -> kext_tools -> notify -> syslog -> IPConfig -> mDNS -> DA -> hostnamed"

# ---- launchd daemon + launchctl ---------------------------------------------
# build.sh ~1433-1493. launchd (bsd.prog.mk) consumes the I1a MIG stubs + CF;
# launchctl links CF/ICU/xpc/dispatch/liblaunch via its Makefile + freebsd-shims.
comp "launchd daemon (post-CF)"
run_buildenv "make -C $SRC/launchd/src DESTDIR=$DESTDIR MIGOUT=$MIG_OUT SYSROOT=$SYSROOT all install"
test -x "$DESTDIR/sbin/launchd" || { echo "FAIL: /sbin/launchd not installed"; exit 1; }

comp "launchctl"
mkdir -p "$DESTDIR/bin"
run_buildenv "make -C $SRC/launchd/support DESTDIR=$DESTDIR SYSROOT=$SYSROOT all install"
test -x "$DESTDIR/bin/launchctl" || { echo "FAIL: /bin/launchctl not installed"; exit 1; }
# build.sh ~1487-1492 asserted CF + ICU resolution with chroot ldd. Replace
# with cross DT_NEEDED checks.
needed_check "$DESTDIR/bin/launchctl" "libCoreFoundation"
needed_check "$DESTDIR/bin/launchctl" "lib_FoundationICU"   # TODO verify under cross: soname may be libicucore

# ---- configd ----------------------------------------------------------------
# build.sh ~1511-1543. Generate config.defs MIG stubs (host mig), then build
# configd (bsd.prog.mk) with MIGOUT pointing at them. usr/sbin pre-created.
comp "configd"
mkdir -p "$DESTDIR/usr/sbin"
CONFIGD_MIG="$MIGROOT/configd"
rm -rf "$CONFIGD_MIG"; mkdir -p "$CONFIGD_MIG"
run_mig "$CONFIGD_MIG" -I"$SRC/libmach/include" \
    -header config.h -user configUser.c \
    -server configServer.c -sheader configServer.h \
    "$SRC/configd/config.defs" \
    || { echo "FAIL: mig could not process config.defs"; exit 1; }
test -s "$CONFIGD_MIG/configServer.c" || { echo "FAIL: mig produced no configServer.c"; exit 1; }
run_buildenv "make -C $SRC/configd DESTDIR=$DESTDIR SYSROOT=$SYSROOT MIGOUT=$CONFIGD_MIG all install"
test -x "$DESTDIR/usr/sbin/configd" || { echo "FAIL: /usr/sbin/configd not installed"; exit 1; }
echo "==> CONFIGD-BUILD-OK"
# DROPPED: configtest/notifytest/patterntest/listtest/multitest (host-exec).

# ---- libSystemConfiguration -------------------------------------------------
# build.sh ~1621-1651. Reuses configUser.c MIG stubs (MIGOUT=$CONFIGD_MIG) +
# config_wire.c; links CF + liblaunch. Pre-create the SC include dir.
comp "libSystemConfiguration"
mkdir -p "$DESTDIR/usr/include/SystemConfiguration"
run_buildenv "make -C $SRC/libSystemConfiguration DESTDIR=$DESTDIR SYSROOT=$SYSROOT MIGOUT=$CONFIGD_MIG all install"
sync_sysroot
test -f "$DESTDIR/usr/lib/system/libSystemConfiguration.so.1" \
    || { echo "FAIL: libSystemConfiguration.so.1 not installed"; exit 1; }
test -f "$DESTDIR/usr/include/SystemConfiguration/SCDynamicStore.h" \
    || { echo "FAIL: SCDynamicStore.h not installed"; exit 1; }
# DROPPED: sctest/scnotifytest/scbridgetest (host-exec) + chroot ldconfig/ldd.

# ---- libIOKit + ioreg -------------------------------------------------------
# build.sh ~1910-1939 (lib) and ~2059-2077 (ioreg). libIOKit is bsd.lib.mk;
# pre-create the IOKit include dir. ioreg(8) in build.sh was a standalone `cc`
# link of ioreg.c; cross-shape it through the build env with the cross clang.
comp "libIOKit"
mkdir -p "$DESTDIR/usr/include/IOKit"
run_buildenv "make -C $SRC/libIOKit DESTDIR=$DESTDIR SYSROOT=$SYSROOT all install"
sync_sysroot
test -f "$DESTDIR/usr/lib/system/libIOKit.so.1" || { echo "FAIL: libIOKit.so.1 not installed"; exit 1; }
test -f "$DESTDIR/usr/include/IOKit/IOKitLib.h" || { echo "FAIL: IOKit/IOKitLib.h not installed"; exit 1; }

comp "ioreg(8)"
mkdir -p "$DESTDIR/usr/sbin"
# build.sh ~2064-2071 used a bare `cc -fblocks ... -lIOKit -lCoreFoundation
# -lsystem_kernel -llaunch -lpthread`. Cross-shape: same flags, cross clang +
# sysroot, --allow-shlib-undefined preserved. TODO verify under cross: if
# ioreg gains a Makefile, prefer run_buildenv make over this hand link.
run_buildenv "\$CC -fblocks --sysroot=$SYSROOT \
    -I$SYSROOT/usr/include -L$SYSROOT/usr/lib/system \
    -Wl,-rpath,/usr/lib/system -Wl,--allow-shlib-undefined \
    -o $DESTDIR/usr/sbin/ioreg $SRC/libIOKit/ioreg.c \
    -lIOKit -lCoreFoundation -lsystem_kernel -llaunch -lpthread"
test -x "$DESTDIR/usr/sbin/ioreg" || { echo "FAIL: /usr/sbin/ioreg not installed"; exit 1; }
needed_check "$DESTDIR/usr/sbin/ioreg" "libIOKit"
# DROPPED: iokittest/iokitmatchtest/iokitnotify* (host-exec).

# ---- kext_tools (libkext + OSKext CLIs + kextd) -----------------------------
# build.sh ~1941-2018. SUBDIR build (bsd.prog.mk + bsd.lib.mk) builds
# kextload/kextunload/kextstat/kextdeps + libexec/kextd; installs sys/iocatalogue.h.
comp "kext_tools"
mkdir -p "$DESTDIR/usr/sbin" "$DESTDIR/usr/libexec" "$DESTDIR/usr/include/sys"
run_buildenv "make -C $SRC/kext_tools DESTDIR=$DESTDIR SYSROOT=$SYSROOT all install"
for b in kextload kextunload kextstat kextdeps; do
    test -x "$DESTDIR/usr/sbin/$b" || { echo "FAIL: /usr/sbin/$b not installed"; exit 1; }
done
test -x "$DESTDIR/usr/libexec/kextd" || { echo "FAIL: kextd not installed"; exit 1; }
needed_check "$DESTDIR/usr/sbin/kextload" "libCoreFoundation"
needed_check "$DESTDIR/usr/libexec/kextd" "libCoreFoundation"
# build.sh ~1982-1984 installs the IOCatalogue ABI header for other consumers.
install -m 0644 "$SRC/kext_tools/kextd/iocatalogue.h" "$DESTDIR/usr/include/sys/iocatalogue.h"
echo "==> kext_tools built; sys/iocatalogue.h installed"
# RELOCATED (build.sh ~2000-2018): the kextdeps dependency-resolution smoke
# RAN /usr/sbin/kextdeps under chroot to assert base-before-leaf load order.
# That execs a cross binary — the assertion MOVES to the boot test.
# DROPPED: test_kextd_mach (host-exec).

# ---- Libnotify (libnotify) + notifyd ----------------------------------------
# build.sh ~2122-2177 (lib) and ~2221-2238 (daemon). Generate notify MIG stubs
# (host mig) + the short-name symlinks notifyd/notify_proc include; build
# libnotify (bsd.lib.mk), then notifyd (bsd.prog.mk) against the same MIGOUT.
comp "libnotify MIG stubs (host mig)"
LIBNOTIFY_MIG="$MIGROOT/libnotify"
rm -rf "$LIBNOTIFY_MIG"; mkdir -p "$LIBNOTIFY_MIG"
NOTIFY_INCS="-I$SRC/libmach/include -I$SRC/Libnotify"
for d in notify_ipc notify_old_ipc; do
    note "mig: ${d}.defs"
    run_mig "$LIBNOTIFY_MIG" $NOTIFY_INCS \
        -header "${d}.h" -user "${d}User.c" \
        -server "${d}Server.c" -sheader "${d}Server.h" \
        "$SRC/Libnotify/${d}.defs" \
        || { echo "FAIL: mig could not process ${d}.defs"; exit 1; }
done
for f in notify_ipc.h notify_ipcUser.c; do
    test -s "$LIBNOTIFY_MIG/$f" || { echo "FAIL: mig produced no $f from notify_ipc.defs"; exit 1; }
done
# build.sh ~2157-2161: short-name symlinks (subsystem is notify_ipc; sources
# include notifyServer.h / notifyUser.h).
( cd "$LIBNOTIFY_MIG"
  ln -sf notify_ipc.h notifyServer.h
  ln -sf notify_ipc.h notifyUser.h
  ln -sf notify_ipcServer.c notifyServer.c
  ln -sf notify_ipcUser.c notifyUser.c )

comp "libnotify"
mkdir -p "$DESTDIR/usr/lib/system"
run_buildenv "make -C $SRC/Libnotify DESTDIR=$DESTDIR SYSROOT=$SYSROOT MIGOUT=$LIBNOTIFY_MIG all install"
sync_sysroot
test -f "$DESTDIR/usr/lib/system/libnotify.so.1" || { echo "FAIL: libnotify.so.1 not installed"; exit 1; }
echo "==> NOTIFY-LIB-OK"

comp "notifyd"
mkdir -p "$DESTDIR/usr/sbin"
run_buildenv "make -C $SRC/Libnotify/notifyd DESTDIR=$DESTDIR SYSROOT=$SYSROOT MIGOUT=$LIBNOTIFY_MIG all install"
test -x "$DESTDIR/usr/sbin/notifyd" || { echo "FAIL: /usr/sbin/notifyd not installed"; exit 1; }
echo "==> NOTIFYD-BUILD-OK"

# ---- ASL / syslog stack -----------------------------------------------------
# build.sh ~2179-2295. Generate asl_ipc MIG stubs (host mig); build
# libsystem_asl (lib) + syslogd + aslmanager + syslog(1) (progs), all against
# MIGOUT=$ASL_MIG. aslcommon is a static lib pulled in by the component Makefiles.
comp "ASL MIG stubs (host mig)"
ASL_MIG="$MIGROOT/asl"
rm -rf "$ASL_MIG"; mkdir -p "$ASL_MIG"
ASL_INCS="-I$SRC/libmach/include -I$SRC/syslog/aslcommon -I$SRC/syslog/libsystem_asl.tproj/include"
run_mig "$ASL_MIG" $ASL_INCS \
    -header asl_ipc.h -user asl_ipcUser.c \
    -server asl_ipcServer.c -sheader asl_ipcServer.h \
    "$SRC/syslog/aslcommon/asl_ipc.defs" \
    || { echo "FAIL: mig could not process asl_ipc.defs"; exit 1; }
for f in asl_ipc.h asl_ipcUser.c; do
    test -s "$ASL_MIG/$f" || { echo "FAIL: mig produced no $f from asl_ipc.defs"; exit 1; }
done

comp "libsystem_asl"
mkdir -p "$DESTDIR/usr/lib/system"
run_buildenv "make -C $SRC/syslog/libsystem_asl.tproj DESTDIR=$DESTDIR SYSROOT=$SYSROOT MIGOUT=$ASL_MIG all install"
sync_sysroot
test -f "$DESTDIR/usr/lib/system/libsystem_asl.so.1" || { echo "FAIL: libsystem_asl.so.1 not installed"; exit 1; }
test -f "$DESTDIR/usr/include/asl.h" || { echo "FAIL: /usr/include/asl.h not installed"; exit 1; }
echo "==> ASL-LIB-OK"

comp "syslogd"
mkdir -p "$DESTDIR/usr/sbin"
run_buildenv "make -C $SRC/syslog/syslogd.tproj DESTDIR=$DESTDIR SYSROOT=$SYSROOT MIGOUT=$ASL_MIG all install"
test -x "$DESTDIR/usr/sbin/syslogd" || { echo "FAIL: /usr/sbin/syslogd not installed"; exit 1; }
echo "==> SYSLOGD-BUILD-OK"

comp "aslmanager"
run_buildenv "make -C $SRC/syslog/aslmanager.tproj DESTDIR=$DESTDIR SYSROOT=$SYSROOT MIGOUT=$ASL_MIG all install"
test -x "$DESTDIR/usr/sbin/aslmanager" || { echo "FAIL: /usr/sbin/aslmanager not installed"; exit 1; }
echo "==> ASLMANAGER-BUILD-OK"

comp "syslog(1) CLI"
mkdir -p "$DESTDIR/usr/bin"
run_buildenv "make -C $SRC/syslog/util.tproj DESTDIR=$DESTDIR SYSROOT=$SYSROOT MIGOUT=$ASL_MIG all install"
test -x "$DESTDIR/usr/bin/syslog" || { echo "FAIL: /usr/bin/syslog not installed"; exit 1; }
echo "==> SYSLOG-CLI-BUILD-OK"

# ---- IPConfiguration --------------------------------------------------------
# build.sh ~2297-2391. Generate ipconfig.defs MIG stubs (host mig); build
# ipconfigd (bsd.prog.mk) with MIGOUT. build.sh also hand-`cc`-links the
# `ipconfig` CLI from ipconfig.c + the MIG user stub; cross-shape that link.
comp "ipconfigd"
mkdir -p "$DESTDIR/usr/sbin"
IPCFG_MIG="$MIGROOT/ipcfg"
rm -rf "$IPCFG_MIG"; mkdir -p "$IPCFG_MIG"
run_mig "$IPCFG_MIG" -I"$SRC/libmach/include" -I"$SRC/IPConfiguration" \
    -header ipconfig.h -user ipconfigUser.c \
    -server ipconfigServer.c -sheader ipconfigServer.h \
    "$SRC/IPConfiguration/ipconfig.defs" \
    || { echo "FAIL: mig could not process ipconfig.defs"; exit 1; }
test -s "$IPCFG_MIG/ipconfigServer.c" || { echo "FAIL: mig produced no ipconfigServer.c"; exit 1; }
run_buildenv "make -C $SRC/IPConfiguration DESTDIR=$DESTDIR SYSROOT=$SYSROOT MIGOUT=$IPCFG_MIG all install"
test -x "$DESTDIR/usr/sbin/ipconfigd" || { echo "FAIL: /usr/sbin/ipconfigd not installed"; exit 1; }

comp "ipconfig(8) CLI"
# build.sh ~2376-2388 bare `cc` link. Same flags (-Wno-macro-redefined,
# --allow-shlib-undefined) under the cross clang.  TODO verify under cross.
run_buildenv "\$CC --sysroot=$SYSROOT \
    -I$IPCFG_MIG -I$SRC/IPConfiguration \
    -I$SRC/launchd/liblaunch -I$SRC/launchd/freebsd-shims \
    -I$SYSROOT/usr/include -L$SYSROOT/usr/lib/system \
    -Wno-macro-redefined -Wl,-rpath,/usr/lib/system -Wl,--allow-shlib-undefined \
    -o $DESTDIR/usr/sbin/ipconfig \
    $SRC/IPConfiguration/ipconfig.c $IPCFG_MIG/ipconfigUser.c \
    -llaunch -lsystem_kernel"
test -x "$DESTDIR/usr/sbin/ipconfig" || { echo "FAIL: /usr/sbin/ipconfig not built"; exit 1; }
echo "==> ipconfigd + ipconfig built"
# DROPPED: ipconfigtest/ipconfigrpctest (host-exec).

# ---- mDNSResponder + libdns_sd ----------------------------------------------
# build.sh ~2400-2461. mDNSResponder daemon (bsd.prog.mk) then libdns_sd
# (bsd.lib.mk). No MIG surface in these blocks.
comp "mDNSResponder"
mkdir -p "$DESTDIR/usr/sbin"
run_buildenv "make -C $SRC/mDNSResponder DESTDIR=$DESTDIR SYSROOT=$SYSROOT all install"
test -x "$DESTDIR/usr/sbin/mDNSResponder" || { echo "FAIL: /usr/sbin/mDNSResponder not installed"; exit 1; }

comp "libdns_sd"
run_buildenv "make -C $SRC/mDNSResponder/libdns_sd DESTDIR=$DESTDIR SYSROOT=$SYSROOT all install"
sync_sysroot
test -f "$DESTDIR/usr/lib/system/libdns_sd.so.1" || { echo "FAIL: libdns_sd.so.1 not installed"; exit 1; }
test -f "$DESTDIR/usr/include/dns_sd.h"          || { echo "FAIL: /usr/include/dns_sd.h not installed"; exit 1; }
echo "==> mDNSResponder + libdns_sd built"
# DROPPED: mdnstest/dnssdtest (host-exec).

# ---- DiskArbitration --------------------------------------------------------
# build.sh ~2463-2504. diskarbitrationd (bsd.prog.mk) links libIOKit (kernel
# notify channel). Built AFTER libIOKit so -lIOKit resolves from the staged
# /usr/lib/system. build.sh asserted libIOKit resolution with chroot ldd ->
# replace with cross DT_NEEDED check.
comp "diskarbitrationd"
mkdir -p "$DESTDIR/usr/sbin"
run_buildenv "make -C $SRC/DiskArbitration DESTDIR=$DESTDIR SYSROOT=$SYSROOT all install"
test -x "$DESTDIR/usr/sbin/diskarbitrationd" || { echo "FAIL: /usr/sbin/diskarbitrationd not installed"; exit 1; }
needed_check "$DESTDIR/usr/sbin/diskarbitrationd" "libIOKit"
echo "==> diskarbitrationd built"
# DROPPED: datest (host-exec).

# ---- hostnamed --------------------------------------------------------------
# build.sh ~2506-2607. One-shot daemon; no MIG surface. bsd.prog.mk links
# SC/CF/dispatch/BlocksRuntime/system_kernel via its Makefile (-fblocks).
comp "hostnamed"
mkdir -p "$DESTDIR/usr/sbin"
run_buildenv "make -C $SRC/hostnamed DESTDIR=$DESTDIR SYSROOT=$SYSROOT all install"
test -x "$DESTDIR/usr/sbin/hostnamed" || { echo "FAIL: /usr/sbin/hostnamed not installed"; exit 1; }
echo "==> hostnamed built"
# DROPPED: hostnametest/hostnameprefset/hostnamedhcpset/hostnamedmdnsset
#          (all host-exec CI fixtures — moved to the boot test).

tier "DONE : system layer (Tiers 0-2) staged into $DESTDIR for $T/$TA"
echo "==> nextbsd-userland cross build complete"
