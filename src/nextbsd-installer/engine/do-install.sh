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
# Usage: do-install.sh -d <disk> -u <user> -p <passfile> -H <hostname> [-U]
set -eu

DISK="" USER_NAME="" PASSFILE="" HOSTNAME_="" UPGRADE=0
MNT=/tmp/nbi-mnt
SRC=/

while getopts "d:u:p:H:U" o; do
	case "$o" in
		d) DISK=$OPTARG ;;
		u) USER_NAME=$OPTARG ;;
		p) PASSFILE=$OPTARG ;;
		H) HOSTNAME_=$OPTARG ;;
		U) UPGRADE=1 ;;
		*) echo "usage: do-install.sh -d disk -u user -p passfile -H hostname [-U]" >&2; exit 2 ;;
	esac
done
[ -n "$DISK" ] && [ -n "$USER_NAME" ] && [ -n "$HOSTNAME_" ] || {
	echo "do-install.sh: missing required args" >&2; exit 2; }

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
	status "Creating UFS root (label ROOTFS)"
	# -L ROOTFS is the linchpin: cloned fstab + kernel-baked root both find it.
	run newfs -U -L ROOTFS "/dev/${DISK}p3"
	run newfs_msdos -L EFI "/dev/${DISK}p2"
	progress 14
fi

# --- 3. Mount target --------------------------------------------------------
status "Mounting target"
run mkdir -p "$MNT"
run mount "/dev/ufs/ROOTFS" "$MNT"
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
	efibootmgr --create --activate --label NextBSD \
		--loader "$EFIMNT/EFI/BOOT/$EFIFILE" >/dev/null 2>&1 || \
		status "efibootmgr entry skipped (removable-path boot still installed)"
fi
run umount "$EFIMNT"
progress 94

# --- 7. Account + hostname --------------------------------------------------
status "Creating admin account + hostname"
# Primary admin in wheel; root left locked.
run pw -R "$MNT" useradd -n "$USER_NAME" -m -G wheel -s /bin/sh
if [ -n "$PASSFILE" ] && [ -f "$PASSFILE" ]; then
	if [ "${NEXTBSD_DRYRUN:-0}" = 1 ]; then echo "DRYRUN: set password for $USER_NAME"
	else pw -R "$MNT" usermod "$USER_NAME" -h 0 < "$PASSFILE"; fi
fi
# Hostname: NextBSD is launchd-native (no rc.conf). hostnamed owns the live
# value; persistence is via its SCPreferences store, NOT /etc/rc.conf.
# TODO(engine): write the hostname through the hostnamed/SCPreferences plist
# the userland overlay uses; placeholder writes /etc/myname for now.
run sh -c "echo '$HOSTNAME_' > '$MNT/etc/myname'"
progress 98

# --- 8. Done ----------------------------------------------------------------
status "Finalizing"
run umount "$MNT" || true
progress 100
echo "DONE"
