#!/bin/sh
set -eu

disk=${1:?usage: write_prepared_phone_card.sh DISK IMAGE.zst}
image=${2:?usage: write_prepared_phone_card.sh DISK IMAGE.zst}

case "$disk" in
  /dev/disk[0-9]*) ;;
  *) echo "refusing unexpected disk path: $disk" >&2; exit 2 ;;
esac
test -f "$image"

info=$(diskutil info "$disk")
printf '%s\n' "$info" | grep -q 'Whole: *Yes'
printf '%s\n' "$info" | grep -q 'Device Location: *External'
printf '%s\n' "$info" | grep -q 'Removable Media: *Removable'
printf '%s\n' "$info" | grep -q 'Disk Size: *63\.9 GB (63864569856 Bytes)'
printf '%s\n' "$info" | grep -q 'Media Read-Only: *No'

raw=/dev/r${disk#/dev/}
diskutil unmountDisk force "$disk"
/opt/homebrew/bin/zstd -dc "$image" | dd of="$raw" bs=8m
sync
diskutil eject "$disk"
