#!/bin/sh
# probe-disks.sh — enumerate candidate install disks for the front-end.
#
# Emits one record per line, tab-separated, in a stable machine-readable form
# the C++ front-end parses:
#
#   DISK<TAB>dev<TAB>size<TAB>model<TAB>is_media(0|1)
#   VOL<TAB>dev<TAB>label<TAB>size<TAB>fstype<TAB>note
#   MEDIA<TAB>dev            # the booted install medium (locked)
#   DMI<TAB>model
#
# Read-only: this only inspects GEOM/CAM, it never writes. FreeBSD-only.
set -eu

# The booted install medium — locked in the front-end so you can't install onto
# what you booted. Two sources: a cd9660/cd node, AND the physical disk backing
# the live root (/). The common case is a USB the image was written to (e.g.
# da0), which the old cd-only check missed — so it was offered as a target.
media=""
for d in /dev/iso9660/* /dev/cd0; do
	[ -e "$d" ] || continue
	media="${d##*/}"
	break
done
# Resolve the disk under / through glabel (e.g. ufs/ROOTFS -> da0p3 -> da0).
root_prov="$(mount -p 2>/dev/null | awk '$2=="/"{print $1; exit}')"
root_prov="${root_prov#/dev/}"
boot_disk="$(glabel status 2>/dev/null | awk -v L="$root_prov" '$1==L{print $3; exit}')"
[ -z "$boot_disk" ] && boot_disk="$root_prov"
boot_disk="$(printf '%s' "$boot_disk" | sed -E 's/[ps][0-9]+$//')"
[ -n "$media" ]     && printf 'MEDIA\t%s\n' "$media"
[ -n "$boot_disk" ] && printf 'MEDIA\t%s\n' "$boot_disk"

# DMI model for the hostname suggestion. Lenovo puts the friendly name
# ("ThinkPad T460s") in smbios.system.version and the bare MTM code
# ("20FAS0GL00") in system.product; most other vendors use system.product.
dmi_maker="$(kenv -q smbios.system.maker 2>/dev/null || true)"
case "$dmi_maker" in
	*[Ll][Ee][Nn][Oo][Vv][Oo]*) model="$(kenv -q smbios.system.version 2>/dev/null || true)" ;;
	*)                            model="$(kenv -q smbios.system.product 2>/dev/null || true)" ;;
esac
[ -z "$model" ] && model="$(kenv -q smbios.system.product 2>/dev/null || true)"
[ -n "$model" ] && printf 'DMI\t%s\n' "$model"

# One DISK line per kern.disks entry, with size + model, then its volumes.
for dev in $(sysctl -n kern.disks 2>/dev/null); do
	bytes="$(diskinfo "$dev" 2>/dev/null | awk '{print $3}')"
	size="$(printf '%s' "${bytes:-0}" | awk '{
		b=$1; u="B"; s=1
		if (b>=1099511627776){u="TB";s=1099511627776}
		else if (b>=1073741824){u="GB";s=1073741824}
		else if (b>=1048576){u="MB";s=1048576}
		printf "%.0f %s", b/s, u }')"
	model="$(diskinfo -v "$dev" 2>/dev/null | awk -F'#' '/Disk descr/{gsub(/^[ \t]+|[ \t]+$/,"",$1); print $1}')"
	is_media=0
	{ [ "$dev" = "$media" ] || [ "$dev" = "$boot_disk" ]; } && is_media=1
	printf 'DISK\t%s\t%s\t%s\t%d\n' "$dev" "${size:-?}" "${model:-disk}" "$is_media"

	# Volumes: GPT partitions with their labels + fs type.
	gpart show -l "$dev" 2>/dev/null | awk -v d="$dev" '
		/freebsd-|efi|ms-basic|fat|linux/ {
			label=$4; if (label=="-") label="(unlabeled)"
			print "VOL\t" d "\t" label "\t" $2 "\t" $5 "\t"
		}' 2>/dev/null || true
done
