#!/bin/sh
# assemble-image.sh — Phase 1: assemble a minimal, CI-only bootable NextBSD .img
# NATIVELY on the Linux runner, from the four already-cross-built artifacts, with
# no FreeBSD VM. See pkgdemon.github.io/nextbsd-native-image-assembly-plan.html.
#
# This is the "prove the assembly" step. It ports build.sh's makefs/mkimg recipe
# (the only FreeBSD-host pieces) to host-built tools, ingests the cross-built
# userland tarball instead of rebuilding it in a VM, and skips the pkg/toolchain
# phase and the live ISO entirely. amd64 first; arm64 is a follow-up (Phase 4).
#
# Inputs (env):
#   ARCH        amd64 | arm64                       (default amd64)
#   WORK        scratch dir                          (default /tmp/nbimg)
#   OUT         output dir for disk.img              (default $WORK/out)
#   BASE_TGZ    nextbsd-base-$ARCH.tar.gz            (compat base = whole rootfs)
#   KERNEL_TGZ  nextbsd-kernel-$ARCH.tar.gz          (-> /boot/kernel/kernel)
#   MODULES_TGZ space-separated kext tarball(s)      (-> /System/Library/Extensions)
#   USERLAND_TGZ nextbsd-userland-$ARCH.tar.gz       (Darwin system layer)
#   OVERLAY     dir whose contents overlay the rootfs (authoritative /etc + plists)
#   SRC         freebsd-src checkout                 (default /usr/src)
# Output: $OUT/disk.img
set -eu

ARCH="${ARCH:-amd64}"
WORK="${WORK:-/tmp/nbimg}"
OUT="${OUT:-$WORK/out}"
SRC="${SRC:-/usr/src}"
ROOTFS="$WORK/rootfs"
TOOLS="$WORK/tools"            # host-built makefs/mkimg land here
case "$ARCH" in
  amd64)  TARGET_ARCH=amd64;  EFI_NAME=BOOTX64.EFI ;;
  arm64)  TARGET_ARCH=aarch64; EFI_NAME=BOOTAA64.EFI ;;
  *) echo "unsupported ARCH=$ARCH" >&2; exit 1 ;;
esac

log() { echo "==> $*"; }

# ---------------------------------------------------------------------------
# 1. Host-build makefs + mkimg (Linux binaries) from /usr/src.
#    These are FreeBSD bootstrap tools that build+run on a non-FreeBSD host.
#    Pattern mirrors how the cross build host-builds migcom. The exact
#    incantation is the most likely Phase-1 iteration point; keep it isolated.
# ---------------------------------------------------------------------------
build_host_tools() {
    if command -v makefs >/dev/null 2>&1 && command -v mkimg >/dev/null 2>&1; then
        log "makefs/mkimg already on PATH"; return
    fi
    : "${CROSS_BINDIR:?CROSS_BINDIR must be set (the kernel-toolchain image provides it)}"
    MKPY="./tools/build/make.py --cross-bindir=$CROSS_BINDIR TARGET=$ARCH TARGET_ARCH=$TARGET_ARCH"
    # makefs/mkimg are host (build) tools that already live in the baked /usr/src.
    # They compile fine in the _legacy stage but link against the build-tool libs
    # in obj-tools (libnetbsd/libutil/libsbuf). The baked kernel-toolchain built
    # libnetbsd/libutil (the kernel needs them) but NOT libsbuf — so first run
    # bootstrap-tools to populate the full build-tool lib set, then the _legacy
    # stage builds + links makefs/mkimg into WORLDTMP/legacy as runner-native bins.
    # makefs links -lsbuf; the baked kernel-toolchain has libnetbsd/libutil in
    # obj-tools but NOT libsbuf. makefs's link line also searches the general
    # -L${WORLDTMP}/legacy/usr/lib, so build libsbuf there by listing it FIRST in
    # LOCAL_LEGACY_DIRS (the legacy stage builds dirs in order), before makefs/mkimg
    # which then resolve -lsbuf from legacy/usr/lib.
    log "_legacy: build libsbuf + makefs + mkimg as runner-native host tools"
    ( cd "$SRC" && $MKPY \
        LOCAL_LEGACY_DIRS="lib/libsbuf usr.sbin/makefs usr.bin/mkimg" \
        _legacy )
    mkdir -p "$TOOLS"
    for name in makefs mkimg; do
        b="$(find /usr/obj -type f -name "$name" -perm -u+x 2>/dev/null | head -1)"
        [ -n "$b" ] && install -m755 "$b" "$TOOLS/$name" \
            || { echo "ERROR: no host $name after bootstrap-tools + _legacy" >&2; exit 1; }
    done
    export PATH="$TOOLS:$PATH"
    log "host tools: $(command -v makefs) | $(command -v mkimg)"
}

# ---------------------------------------------------------------------------
# 2. Stage the rootfs: base = whole tree, then kernel/modules/userland, then
#    the authoritative overlay last (last writer wins, matching build.sh).
# ---------------------------------------------------------------------------
stage_rootfs() {
    log "staging rootfs at $ROOTFS"
    rm -rf "$ROOTFS"; mkdir -p "$ROOTFS"
    tar -C "$ROOTFS" -xzf "$BASE_TGZ"
    mkdir -p "$ROOTFS/etc" "$ROOTFS/var" "$ROOTFS/boot/kernel" "$ROOTFS/System/Library/Extensions"
    # kernel -> /boot/kernel/kernel
    tmpk="$WORK/k"; rm -rf "$tmpk"; mkdir -p "$tmpk"; tar -C "$tmpk" -xzf "$KERNEL_TGZ"
    kbin="$(find "$tmpk" -type f -name kernel | head -1)"
    [ -n "$kbin" ] || { echo "ERROR: kernel binary not found in $KERNEL_TGZ" >&2; exit 1; }
    install -m555 "$kbin" "$ROOTFS/boot/kernel/kernel"
    # driver kexts -> /System/Library/Extensions
    for m in ${MODULES_TGZ:-}; do [ -f "$m" ] && tar -C "$ROOTFS/System/Library/Extensions" -xzf "$m"; done
    # Darwin userland (raw tar rooted at /)
    tar -C "$ROOTFS" -xzf "$USERLAND_TGZ"
    # overlay last
    [ -n "${OVERLAY:-}" ] && [ -d "$OVERLAY" ] && cp -aR "$OVERLAY/." "$ROOTFS/"
}

# ---------------------------------------------------------------------------
# 3. Fix up the staged tree for boot: kext model, ownership, offline DBs.
#    NextBSD loads drivers as KEXTS (OSKext/kextd), not klds — so strip the
#    .ko/firmware tree and set kext perms; no kldxref needed at boot.
# ---------------------------------------------------------------------------
fixup_rootfs() {
    log "fixup: strip .ko/firmware, kext perms, offline DBs"
    rm -f "$ROOTFS"/boot/kernel/*.ko 2>/dev/null || true
    rm -rf "$ROOTFS/boot/firmware" 2>/dev/null || true
    # kext auth: root:wheel, group/other not writable
    if [ -d "$ROOTFS/System/Library/Extensions" ]; then
        chmod -R go-w "$ROOTFS/System/Library/Extensions" || true
    fi
    # offline passwd / login.conf DBs (host pwd_mkdb/cap_mkdb if available)
    if [ -f "$ROOTFS/etc/master.passwd" ] && command -v pwd_mkdb >/dev/null 2>&1; then
        pwd_mkdb -p -d "$ROOTFS/etc" "$ROOTFS/etc/master.passwd" || \
          echo "WARN: pwd_mkdb failed (bake pwd.db into overlay instead)" >&2
    fi
    if [ -f "$ROOTFS/etc/login.conf" ] && command -v cap_mkdb >/dev/null 2>&1; then
        cap_mkdb "$ROOTFS/etc/login.conf" || echo "WARN: cap_mkdb failed" >&2
    fi
    # bake into the image as uid/gid 0 (mtree manifest re-applies setuid below)
}

# ---------------------------------------------------------------------------
# 4. makefs UFS root + FAT ESP, then mkimg GPT — ported verbatim from build.sh.
# ---------------------------------------------------------------------------
assemble() {
    mkdir -p "$OUT"
    log "makefs ffs (root)"
    makefs -t ffs -B little -o version=2,label=ROOTFS,softupdates=1 -b 512m \
        "$WORK/rootfs.ufs" "$ROOTFS"

    log "EFI System Partition ($EFI_NAME)"
    espdir="$WORK/esp-stage"; rm -rf "$espdir"; mkdir -p "$espdir/EFI/BOOT"
    if   [ -f "$ROOTFS/boot/loader_lua.efi" ]; then cp "$ROOTFS/boot/loader_lua.efi" "$espdir/EFI/BOOT/$EFI_NAME"
    elif [ -f "$ROOTFS/boot/loader.efi" ];     then cp "$ROOTFS/boot/loader.efi"     "$espdir/EFI/BOOT/$EFI_NAME"
    else echo "ERROR: no loader.efi in rootfs/boot" >&2; exit 1; fi
    makefs -t msdos -o fat_type=32 -o sectors_per_cluster=1 -o volume_label=EFISYS \
        -s 33292k "$WORK/esp.img" "$espdir"

    log "mkimg GPT -> $OUT/disk.img"
    if [ "$ARCH" = amd64 ]; then
        for f in boot/pmbr boot/gptboot; do
            [ -f "$ROOTFS/$f" ] || { echo "ERROR: rootfs/$f missing" >&2; exit 1; }
        done
        mkimg -s gpt -f raw \
            -b "$ROOTFS/boot/pmbr" \
            -p freebsd-boot/bootfs:="$ROOTFS/boot/gptboot" \
            -p efi/efiboot0:="$WORK/esp.img" \
            -p freebsd-ufs/ROOTFS:="$WORK/rootfs.ufs" \
            -o "$OUT/disk.img"
    else
        # arm64: UEFI only — no pmbr/gptboot
        mkimg -s gpt -f raw \
            -p efi/efiboot0:="$WORK/esp.img" \
            -p freebsd-ufs/ROOTFS:="$WORK/rootfs.ufs" \
            -o "$OUT/disk.img"
    fi
    ls -lh "$OUT/disk.img"
}

build_host_tools
stage_rootfs
fixup_rootfs
assemble
log "done: $OUT/disk.img"
