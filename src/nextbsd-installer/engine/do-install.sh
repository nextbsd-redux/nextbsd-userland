#!/bin/sh
# do-install.sh — the NextBSD whole-disk install engine.
#
# Driven by the front-end (which parses the PROGRESS/STATUS lines below), but
# also runnable standalone. Honors NEXTBSD_DRYRUN=1 (print every destructive
# command instead of running it) so it's safe to exercise off-target.
#
# Design notes baked in:
#   * Whole-disk GPT: freebsd-boot (BIOS) + EFI (ESP) + freebsd-ufs root.
#   * ...EXCEPT on a Raspberry Pi 5-family board, which cannot boot that at
#     all. There is no UEFI and no loader(8): the EEPROM bootloader reads
#     config.txt off a FAT partition and enters the kernel directly. That
#     needs MBR + FAT32 + UFS, and is selected automatically -- see BOARD.
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

# Never leave the probe mount behind, whatever happens next -- including a
# dry run, a failure, or an interrupt. The installer inspects media it does
# not own; it should leave the machine exactly as it found it.
SRCBOOT=/tmp/nbi-srcboot
cleanup() {
	umount "$SRCBOOT" 2>/dev/null || true
	rmdir "$SRCBOOT" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

DISK="" UPGRADE=0
MNT=/tmp/nbi-mnt
SRC=/
# Root label for the INSTALLED system — deliberately NOT "ROOTFS" (the live
# install medium's label). A shared label makes a leftover/failed install a
# boot-hijacker: two ufs/ROOTFS providers and the kernel mounts the wrong one.
# The cloned fstab + a loader.conf override (step 5b) point the target here.
LABEL=NEXTBSD

# --- Which boot layout does this machine need? ------------------------------
# Detected rather than flagged, so the same live image does the right thing
# wherever it boots. The device tree's root "compatible" is the honest source:
# a Pi 500+ reads "raspberrypi,500" + "brcm,bcm2712". Anything matching
# brcm,bcm2712 is a Pi 5-family board (5 B, 500, 500+, CM5) and needs the
# firmware boot layout rather than GPT+ESP.
#
# ofwdump(8) reads it straight out of the tree the kernel booted with, and
# ships in base. It exits non-zero on a machine with no OFW/FDT (amd64), which
# is exactly the "generic" answer.
# Which boot method does this machine need? Ask the medium that actually
# booted it, not the hardware.
#
# A machine booted by Raspberry Pi firmware has a FAT partition holding
# config.txt -- that file IS the boot method, and its presence is proof rather
# than inference. Copying that arrangement to the target is then correct by
# construction: it demonstrably works on this hardware, which no amount of
# board detection can promise.
#
# Two supporting checks keep this honest. machdep.efi_map exists on every UEFI
# boot and no direct-firmware boot, so a Pi running the EDK2 UEFI firmware is
# correctly sent down the ESP path even though it is a Pi. And a config.txt on
# some unrelated FAT volume must not fool us, so the partition also has to
# carry the kernel it names.
#
# find_boot_medium sets FOUND and leaves the volume mounted at $SRCBOOT
# (cleaned up by the EXIT trap above no matter how we leave).
FOUND=""
find_boot_medium() {
	# Start from a known state: a leftover mount here -- from an interrupted
	# earlier run -- would make every mount below fail and the machine would
	# silently look like it has no boot medium at all.
	umount "$SRCBOOT" 2>/dev/null || true
	mkdir -p "$SRCBOOT"
	for cand in /dev/da*s1 /dev/da*p1 /dev/nda*s1 /dev/nda*p1 \
	            /dev/mmcsd*s1 /dev/mmcsd*p1 /dev/ada*s1 /dev/ada*p1; do
		[ -e "$cand" ] || continue
		# Never look at the disk we are about to erase.
		case "$cand" in /dev/${DISK}*) continue ;; esac
		# Read-only: we are inspecting media we do not own, and one of them
		# is the volume this system booted from. Nothing here needs to write.
		mount -o ro -t msdosfs "$cand" "$SRCBOOT" 2>/dev/null || continue
		if [ -f "$SRCBOOT/config.txt" ]; then
			# config.txt names the kernel; a boot medium has it.
			k=$(sed -n 's/^[[:space:]]*kernel=[[:space:]]*//p' "$SRCBOOT/config.txt" 2>/dev/null | tail -1)
			if [ -f "$SRCBOOT/${k:-kernel8.img}" ]; then
				FOUND=$cand
				return 0
			fi
		fi
		umount "$SRCBOOT" 2>/dev/null || true
	done
	return 1
}

detect_board() {
	# UEFI wins wherever it exists, Pi or not: an ESP is what that firmware
	# looks for.
	if sysctl -n machdep.efi_map >/dev/null 2>&1; then
		echo generic
		return
	fi
	if find_boot_medium; then
		echo fdtboot
		return
	fi
	echo generic
}

while getopts "d:U" o; do
	case "$o" in
		d) DISK=$OPTARG ;;
		U) UPGRADE=1 ;;
		*) echo "usage: do-install.sh -d disk [-U]" >&2; exit 2 ;;
	esac
done
[ -n "$DISK" ] || { echo "do-install.sh: missing -d <disk>" >&2; exit 2; }

# Detect AFTER $DISK is known: find_boot_medium has to skip the install target,
# and with $DISK empty it would happily inspect the very disk we are about to
# erase -- or, if that disk still holds an old config.txt, conclude from it.
BOARD=$(detect_board)
# Overridable for testing and for the case where detection is wrong; the
# installer should never be un-runnable because a probe misfired.
BOARD=${NEXTBSD_BOARD:-$BOARD}

# On the Pi there is no loader to override the kernel's baked-in root device,
# so the target's label has to match what the kernel already looks for.
[ "$BOARD" = fdtboot ] && LABEL=ROOTFS

progress() { printf 'PROGRESS\t%s\n' "$1"; }
status()   { printf 'STATUS\t%s\n'   "$1"; }
run() {
	if [ "${NEXTBSD_DRYRUN:-0}" = 1 ]; then echo "DRYRUN: $*"; else "$@"; fi
}

# --- 1. Partition (skipped on upgrade — keep the existing layout) ------------
if [ "$UPGRADE" = 0 ]; then
	if [ "$BOARD" = fdtboot ]; then
		# MBR, not GPT, and a plain FAT32 data partition rather than an
		# ESP. The BCM2712 bootloader lives in EEPROM: it looks for a FAT
		# partition holding config.txt, reads the DTB and kernel named
		# there, and enters the kernel directly at EL2. No UEFI, no
		# loader(8), so nothing here is negotiable.
		#
		# fat32lba (type 0x0c) is what Raspberry Pi OS itself uses; a
		# plain fat32 (0x0b) is CHS-addressed and wrong.
		status "Partitioning $DISK (MBR: FAT32 boot + freebsd)"
		progress 4
		run gpart destroy -F "$DISK" 2>/dev/null || true
		run gpart create -s mbr "$DISK"
		run gpart add -t fat32lba -s 100m "$DISK"
		run gpart add -t freebsd "$DISK"
		# The UFS root lives in a BSD label inside the freebsd slice.
		run gpart create -s bsd "/dev/${DISK}s2"
		run gpart add -t freebsd-ufs "/dev/${DISK}s2"
		progress 8

		status "Creating UFS root (label $LABEL)"
		run newfs -U -L "$LABEL" "/dev/${DISK}s2a"
		run newfs_msdos -F 32 -L NEXTBSD "/dev/${DISK}s1"
		progress 14
	else
		status "Partitioning $DISK (GPT: freebsd-boot + EFI + freebsd-ufs)"
		progress 4
		run gpart destroy -F "$DISK" 2>/dev/null || true
		run gpart create -s gpt "$DISK"
		run gpart add -a 4k -s 512k -t freebsd-boot -l bootcode "$DISK"
		run gpart add -a 1m   -s 260m -t efi          -l EFI      "$DISK"
		run gpart add -a 1m            -t freebsd-ufs  -l ROOTFS   "$DISK"
		progress 8

		# --- 2. Filesystems -------------------------------------------
		status "Creating UFS root (label $LABEL)"
		run newfs -U -L "$LABEL" "/dev/${DISK}p3"
		run newfs_msdos -L EFI "/dev/${DISK}p2"
		progress 14
	fi
fi

# Where the root filesystem ended up, so the mount + fstab steps below do not
# each have to re-derive it.
if [ "$BOARD" = fdtboot ]; then
	ROOTPART="${DISK}s2a"
	BOOTPART="${DISK}s1"
else
	ROOTPART="${DISK}p3"
	BOOTPART="${DISK}p2"
fi

# --- 3. Mount target --------------------------------------------------------
# Mount by DEVICE PATH (${DISK}p3), never by ufs label — unambiguous no matter
# what else is attached. (This also dodged the old "Device busy" when the live
# ROOTFS medium shadowed the target's label.)
status "Mounting target"
run mkdir -p "$MNT"
run mount "/dev/$ROOTPART" "$MNT"
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
# loader.conf only means something where there IS a loader. Route (a) on a Pi
# has none -- the firmware enters the kernel directly -- so the label in fstab
# above plus the kernel's compiled-in ROOTDEVNAME are the whole story there.
#
# That leaves one wrinkle worth naming: the baked-in ROOTDEVNAME is
# ufs:/dev/ufs/ROOTFS, and this installer deliberately labels the target
# NEXTBSD so a leftover install medium cannot hijack the boot. On the Pi the
# kernel therefore cannot find root by its baked default and drops to
# mountroot. Until a board kernel ships with a matching default, label the Pi
# root ROOTFS and accept that the live medium must not be left plugged in.
if [ "$BOARD" != fdtboot ]; then
	run sh -c "echo 'vfs.root.mountfrom=\"ufs:/dev/ufs/$LABEL\"' >> '$MNT/boot/loader.conf'"
fi
progress 90

# --- 6. Make the disk bootable ----------------------------------------------
if [ "$BOARD" = fdtboot ]; then
	# The firmware boot partition: config.txt, the board DTB, and the kernel
	# as kernel8.img. No bootcode, no ESP, no loader -- the EEPROM
	# bootloader reads config.txt and enters the kernel itself.
	status "Installing the Raspberry Pi boot partition"
	BOOTMNT=/tmp/nbi-boot
	run mkdir -p "$BOOTMNT"
	run mount -t msdosfs "/dev/$BOOTPART" "$BOOTMNT"

	# kernel8.img is kernel.bin -- the kernel wrapped in an arm64 Linux
	# Image header -- NOT /boot/kernel/kernel, which is an ELF the firmware
	# will not enter. The live medium booted from one, so copy that: it is
	# by definition the kernel this machine is running.
	# $FOUND / $SRCBOOT were established by detect_board() above: the volume
	# holding the config.txt that booted this machine, still mounted.
	if [ -z "$FOUND" ] && [ "${NEXTBSD_DRYRUN:-0}" != 1 ]; then
		echo "do-install.sh: lost the boot medium we detected earlier" >&2
		exit 1
	fi

	# Reproduce the boot setup we are running from, rather than generating one.
	# The medium booted this machine, so its boot partition is PROVEN to work
	# on this hardware: config.txt, the DTB that config.txt names, any
	# overlays, and the firmware blobs a pre-2712 Pi needs. Copying it
	# wholesale is correct by construction, and needs no per-board knowledge --
	# a generated config.txt is only as good as this script's guesses.
	status "Copying the boot setup from ${FOUND:-<dryrun>}"
	if [ -n "$FOUND" ]; then
		# Everything except the noise a desktop OS leaves on a FAT volume.
		for f in "$SRCBOOT"/*; do
			[ -e "$f" ] || continue
			case "$(basename "$f")" in
			.Spotlight-V100|.fseventsd|.Trashes|System\ Volume\ Information) continue ;;
			esac
			run cp -R "$f" "$BOOTMNT/"
		done
		# Report what will actually boot, for the log and for the operator.
		DTB=$(sed -n 's/^[[:space:]]*device_tree=[[:space:]]*//p' "$SRCBOOT/config.txt" 2>/dev/null | tail -1)
		KERN=$(sed -n 's/^[[:space:]]*kernel=[[:space:]]*//p' "$SRCBOOT/config.txt" 2>/dev/null | tail -1)
		status "Boot setup: kernel=${KERN:-<firmware default>} device_tree=${DTB:-<firmware default>}"
	fi

	run umount "$BOOTMNT"
	progress 94
else
	status "Installing bootcode (UEFI loader + BIOS gptboot if present)"
	# BIOS bootcode only when the boot blocks exist (amd64 legacy). EFI-only
	# boxes and arm64 ship no pmbr/gptboot -- skip instead of failing; the ESP
	# loader below is what boots UEFI machines.
	if [ -f "$MNT/boot/pmbr" ] && [ -f "$MNT/boot/gptboot" ]; then
		run gpart bootcode -b "$MNT/boot/pmbr" -p "$MNT/boot/gptboot" -i 1 "$DISK"
	else
		status "No BIOS boot blocks present (EFI-only) — skipping gptboot"
	fi
	# Populate the ESP with the EFI loader at the firmware removable-media path
	# (works with no NVRAM entry). arm64 firmware looks for BOOTAA64.EFI, amd64
	# for BOOTX64.EFI.
	case "$(uname -m)" in
		arm64|aarch64) EFIFILE=BOOTAA64.EFI ;;
		*)             EFIFILE=BOOTX64.EFI  ;;
	esac
	EFIMNT=/tmp/nbi-efi
	run mkdir -p "$EFIMNT" "$MNT/boot/efi"
	run mount -t msdosfs "/dev/$BOOTPART" "$EFIMNT"
	run mkdir -p "$EFIMNT/EFI/BOOT"
	run cp "$MNT/boot/loader.efi" "$EFIMNT/EFI/BOOT/$EFIFILE"
	# Best-effort named UEFI boot entry (non-fatal: the removable path above
	# already boots if the firmware ignores or loses NVRAM entries).
	if [ "${NEXTBSD_DRYRUN:-0}" != 1 ] && command -v efibootmgr >/dev/null 2>&1; then
		efibootmgr 2>/dev/null \
			| sed -n 's/^[[:space:]+-]*Boot\([0-9A-Fa-f]\{4\}\)\*\{0,1\}[[:space:]]\{1,\}NextBSD$/\1/p' \
			| while read -r bootnum; do
				efibootmgr -B -b "$bootnum" >/dev/null 2>&1 || true
			done
		efibootmgr --create --activate --label NextBSD \
			--loader "$EFIMNT/EFI/BOOT/$EFIFILE" >/dev/null 2>&1 || \
			status "efibootmgr entry skipped (removable-path boot still installed)"
	fi
	run umount "$EFIMNT"
	progress 94
fi

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
