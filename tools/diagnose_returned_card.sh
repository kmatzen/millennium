#!/bin/sh
set -eu

partition=${1:?usage: diagnose_returned_card.sh PARTITION OUTPUT_ROOT}
output_root=${2:?usage: diagnose_returned_card.sh PARTITION OUTPUT_ROOT}

case "$partition" in
  /dev/disk*s7) ;;
  *) echo "refusing unexpected persistent partition: $partition" >&2; exit 2 ;;
esac
test -d "$output_root"

work=$(mktemp -d "$output_root/returned-card.XXXXXX")
image="$work/persistent.ext4"
journals=$(mktemp -d /private/tmp/millennium-returned-card-journals.XXXXXX)

dd if="$partition" of="$image" bs=8m
/opt/homebrew/opt/e2fsprogs/sbin/e2fsck -fy "$image"
/opt/homebrew/opt/e2fsprogs/sbin/debugfs -R "rdump /log/journal $journals" "$image"
chmod -R a+rX "$work"
chmod -R a+rX "$journals"
printf 'filesystem-copy=%s\njournals=%s\n' "$work" "$journals"
