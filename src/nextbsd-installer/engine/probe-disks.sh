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

# The booted install medium (cd or the USB the live image was written to), so
# the front-end can lock it. Best-effort: the cd9660 label node or cd0.
media=""
for d in /dev/iso9660/* /dev/cd0; do
	[ -e "$d" ] || continue
	media="${d##*/}"
	break
done
[ -n "$media" ] && printf 'MEDIA\t%s\n' "$media"

# DMI product, for the hostname suggestion.
model="$(kenv -q smbios.system.product 2>/dev/null || true)"
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
	[ "$dev" = "$media" ] && is_media=1
	printf 'DISK\t%s\t%s\t%s\t%d\n' "$dev" "${size:-?}" "${model:-disk}" "$is_media"

	# Volumes: GPT partitions with their labels + fs type.
	gpart show -l "$dev" 2>/dev/null | awk -v d="$dev" '
		/freebsd-|efi|ms-basic|fat|linux/ {
			label=$4; if (label=="-") label="(unlabeled)"
			print "VOL\t" d "\t" label "\t" $2 "\t" $5 "\t"
		}' 2>/dev/null || true
done
