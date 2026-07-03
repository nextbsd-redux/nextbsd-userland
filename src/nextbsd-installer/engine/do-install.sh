#!/bin/sh
# do-install.sh — the NextBSD whole-disk install engine.
#
# Driven by the front-end (which parses the PROGRESS/STATUS lines below), but
# also runnable standalone. Honors NEXTBSD_DRYRUN=1 (print every destructive
# command instead of running it) so it's safe to exercise off-target.
#
# Design notes baked in:
#   * Whole-disk GPT: freebsd-boot (BIOS) + EFI (ESP) + freebsd-ufs root.
#   * UFS root labeled ROOTFS  -> the shipped /etc/fstab (ufs/ROOTFS) and the
#     kernel's baked-in ufs:/dev/ufs/ROOTFS root both resolve with NO edits,
#     and the install is disk-path agnostic (ada0/nvd0/vtbd0 all just work).
#   * Clone with cpdup from the live / (union ISO or plain image alike), then
#     SCRUB the volatile dirs — NextBSD has no rc.d cleanvar/cleartmp, so the
#     installer must hand off a boot-clean /var and /tmp itself.
#
# Usage: do-install.sh -d <disk> [-U]
#
# The installer does NOT create a user or set a hostname: the installed system
# boots with a passwordless root (from the base image) and hostnamed's
# synthesized default name. The operator sets a root password / adds users /
# changes the hostname after first boot.
set -eu

DISK="" UPGRADE=0
MNT=/tmp/nbi-mnt
SRC=/
# Root label for the INSTALLED system — deliberately NOT "ROOTFS" (the live
# install medium's label). A shared label makes a leftover/failed install a
# boot-hijacker: two ufs/ROOTFS providers and the kernel mounts the wrong one.
# The cloned fstab + a loader.conf override (step 5b) point the target here.
LABEL=NEXTBSD

while getopts "d:U" o; do
	case "$o" in
		d) DISK=$OPTARG ;;
		U) UPGRADE=1 ;;
		*) echo "usage: do-install.sh -d disk [-U]" >&2; exit 2 ;;
	esac
done
[ -n "$DISK" ] || { echo "do-install.sh: missing -d <disk>" >&2; exit 2; }

progress() { printf 'PROGRESS\t%s\n' "$1"; }
status()   { printf 'STATUS\t%s\n'   "$1"; }
run() {
	if [ "${NEXTBSD_DRYRUN:-0}" = 1 ]; then echo "DRYRUN: $*"; else "$@"; fi
}

# --- 1. Partition (skipped on upgrade — keep the existing layout) ------------
if [ "$UPGRADE" = 0 ]; then
	status "Partitioning $DISK (GPT: freebsd-boot + EFI + freebsd-ufs)"
	progress 4
	run gpart destroy -F "$DISK" 2>/dev/null || true
	run gpart create -s gpt "$DISK"
	run gpart add -a 4k -s 512k -t freebsd-boot -l bootcode "$DISK"
	run gpart add -a 1m   -s 260m -t efi          -l EFI      "$DISK"
	run gpart add -a 1m            -t freebsd-ufs  -l ROOTFS   "$DISK"
	progress 8

	# --- 2. Filesystems ---------------------------------------------------
	status "Creating UFS root (label $LABEL)"
	run newfs -U -L "$LABEL" "/dev/${DISK}p3"
	run newfs_msdos -L EFI "/dev/${DISK}p2"
	progress 14
fi

# --- 3. Mount target --------------------------------------------------------
# Mount by DEVICE PATH (${DISK}p3), never by ufs label — unambiguous no matter
# what else is attached. (This also dodged the old "Device busy" when the live
# ROOTFS medium shadowed the target's label.)
status "Mounting target"
run mkdir -p "$MNT"
run mount "/dev/${DISK}p3" "$MNT"
progress 16

# --- 4. Clone the running system (cpdup) ------------------------------------
# Works the same on the union ISO and a plain image: cpdup reads / through the
# VFS and (don't-cross-mounts) skips /dev and $MNT automatically. cpdup vendored
# alongside this engine.
status "Cloning base with cpdup"
if [ "$UPGRADE" = 1 ]; then
	# Upgrade: replace base, KEEP data. -o = don't delete extras in the
	# preserved trees; we clone over the top but spare /etc /var /home /Users.
	for keep in etc var home Users usr/local; do
		[ -e "$MNT/$keep" ] && run cpdup -o -i0 "$SRC/$keep" "$MNT/$keep"
	done
	run cpdup -i0 -X /dev/null "$SRC" "$MNT"
else
	run cpdup -i0 "$SRC" "$MNT"
fi
progress 86

# --- 5. Boot-clean the volatile dirs (no rc.d cleanvar/cleartmp on NextBSD) --
status "Scrubbing volatile state"
for d in tmp var/run var/tmp var/log; do
	[ -d "$MNT/$d" ] && run find "$MNT/$d" -mindepth 1 -delete 2>/dev/null || true
done
progress 88

# --- 5b. Point the cloned system at its OWN root label -----------------------
# cpdup copied the source's fstab (ufs/ROOTFS) and loader.conf verbatim, so the
# target still references the live medium's label. Repoint both at $LABEL so the
# installed disk is self-contained and never collides with a ROOTFS medium. The
# kernel's baked ROOTDEVNAME=ufs/ROOTFS is only a fallback; the loader.conf
# vfs.root.mountfrom below takes precedence.
status "Setting root label + boot config ($LABEL)"
[ -f "$MNT/etc/fstab" ] && run sed -i '' -e "s#/dev/ufs/ROOTFS#/dev/ufs/$LABEL#g" "$MNT/etc/fstab"
run sh -c "echo 'vfs.root.mountfrom=\"ufs:/dev/ufs/$LABEL\"' >> '$MNT/boot/loader.conf'"
progress 90

# --- 6. Make the disk bootable ----------------------------------------------
status "Installing bootcode (UEFI loader + BIOS gptboot if present)"
# BIOS bootcode only when the boot blocks exist (amd64 legacy). EFI-only boxes
# and arm64 ship no pmbr/gptboot — skip instead of failing; the ESP loader below
# is what boots UEFI machines.
if [ -f "$MNT/boot/pmbr" ] && [ -f "$MNT/boot/gptboot" ]; then
	run gpart bootcode -b "$MNT/boot/pmbr" -p "$MNT/boot/gptboot" -i 1 "$DISK"
else
	status "No BIOS boot blocks present (EFI-only) — skipping gptboot"
fi
# Populate the ESP with the EFI loader at the firmware removable-media path
# (works with no NVRAM entry). arm64 firmware looks for BOOTAA64.EFI, amd64 for
# BOOTX64.EFI.
case "$(uname -m)" in
	arm64|aarch64) EFIFILE=BOOTAA64.EFI ;;
	*)             EFIFILE=BOOTX64.EFI  ;;
esac
EFIMNT=/tmp/nbi-efi
run mkdir -p "$EFIMNT" "$MNT/boot/efi"
run mount -t msdosfs "/dev/${DISK}p2" "$EFIMNT"
run mkdir -p "$EFIMNT/EFI/BOOT"
run cp "$MNT/boot/loader.efi" "$EFIMNT/EFI/BOOT/$EFIFILE"
# Best-effort named UEFI boot entry (non-fatal: the removable path above already
# boots if the firmware ignores or loses NVRAM entries, e.g. after a CMOS reset).
if [ "${NEXTBSD_DRYRUN:-0}" != 1 ] && command -v efibootmgr >/dev/null 2>&1; then
	# First delete every existing entry labelled exactly "NextBSD" so reinstalls
	# don't stack up duplicate entries in the firmware boot menu. Silent, and the
	# "$" anchor keeps the match exact — other OSes' entries are never touched.
	# efibootmgr lists e.g. "+Boot0001* NextBSD"; capture the 4-hex bootnum.
	efibootmgr 2>/dev/null \
		| sed -n 's/^[[:space:]+-]*Boot\([0-9A-Fa-f]\{4\}\)\*\{0,1\}[[:space:]]\{1,\}NextBSD$/\1/p' \
		| while read -r bootnum; do
			efibootmgr -B -b "$bootnum" >/dev/null 2>&1 || true
		done
	# Then create the single fresh entry.
	efibootmgr --create --activate --label NextBSD \
		--loader "$EFIMNT/EFI/BOOT/$EFIFILE" >/dev/null 2>&1 || \
		status "efibootmgr entry skipped (removable-path boot still installed)"
fi
run umount "$EFIMNT"
progress 94

# --- 7. Account + hostname: intentionally none ------------------------------
# No user is created and no hostname is written. The cloned base ships a
# passwordless root, and hostnamed synthesizes a default hostname at runtime,
# so the system is usable on first boot. The operator sets a root password,
# adds users, and changes the hostname after install.
progress 98

# --- 8. Done ----------------------------------------------------------------
status "Finalizing"
run umount "$MNT" || true
progress 100
echo "DONE"
