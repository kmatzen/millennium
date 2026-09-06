#!/usr/bin/env python3
"""Prepare the pinned official rpi-image-gen tree for Zero 2 W MBR A/B."""

import argparse
from pathlib import Path
import shutil
import subprocess


UPSTREAM_COMMIT = "3f2c916086ad70197945bfc50ef953c1f6035f10"


def replace_once(path, old, new):
    value = path.read_text(encoding="utf-8")
    if value.count(old) != 1:
        raise SystemExit("unexpected pinned upstream content: %s" % path)
    path.write_text(value.replace(old, new), encoding="utf-8")


def replace_count(path, old, new, count):
    value = path.read_text(encoding="utf-8")
    if value.count(old) != count:
        raise SystemExit("unexpected pinned upstream content: %s" % path)
    path.write_text(value.replace(old, new), encoding="utf-8")


def prepare(source, output):
    source = Path(source).resolve()
    output = Path(output).resolve()
    commit = subprocess.run(
        ["git", "-C", str(source), "rev-parse", "HEAD"], check=True,
        text=True, stdout=subprocess.PIPE).stdout.strip()
    if commit != UPSTREAM_COMMIT:
        raise SystemExit("rpi-image-gen must be pinned to %s" % UPSTREAM_COMMIT)
    if output.exists():
        raise SystemExit("refusing to overwrite builder output: %s" % output)
    shutil.copytree(source, output, symlinks=True, ignore=shutil.ignore_patterns(".git"))

    image = output / "image/gpt/ab_userdata/image.yaml"
    replace_once(
        image, "IGconf_device_class\n# X-Env-VarRequires-Valid: string,regex:^(cm4|pi4|cm5|pi5)$",
        "IGconf_device_class\n# X-Env-VarRequires-Valid: string,regex:^(zero2w|cm4|pi4|cm5|pi5)$")
    replace_once(
        image, "Immutable GPT A/B layout for rotational OTA updates,",
        "Immutable MBR/GPT A/B layout for rotational OTA updates,")

    for filename in ("genimage.cfg.in.ext4", "genimage.cfg.in.erofs"):
        layout = output / "image/gpt/ab_userdata" / filename
        replace_once(
            layout,
            'partition-table-type = "gpt"\n      fill = true',
            'partition-table-type = "mbr"\n      extended-partition = 4\n      fill = true')
        replace_count(layout, "partition-type-uuid = F", "partition-type = 0x0c", 3)
        replace_count(layout, "partition-type-uuid = L", "partition-type = 0x83", 3)
        replace_once(
            layout,
            '      file "autoboot.txt" { image = "autoboot.txt" }',
            '      file "autoboot.txt" { image = "autoboot.txt" }\n'
            '      file "bootcode.bin" { image = "bootcode.bin" }\n'
            '      file "start.elf" { image = "boot-start.elf" }')

    pre_image = output / "image/gpt/ab_userdata/pre-image.sh"
    replace_once(
        pre_image,
        'EOF\n\n\n# Write genimage template',
        'EOF\n\n\n# BCM2837 boot ROM (including Zero 2 W) must load the second-stage bootloader\n'
        '# from the first FAT partition before that code can process autoboot.txt.\n'
        'bootcode="${fs}/boot/firmware/bootcode.bin"\n'
        '[ -s "$bootcode" ] || { echo "missing Zero 2 W bootcode.bin" >&2; exit 1; }\n'
        'cp -- "$bootcode" "${genimg_in}/bootcode.bin"\n\n\n'
        '# Presence of start.elf marks BOOTCONFIG as bootable to pre-2711 firmware;\n'
        '# its contents are loaded from the partition selected by autoboot.txt.\n'
        ': > "${genimg_in}/boot-start.elf"\n\n\n'
        '# Write genimage template')

    slot_post = output / "image/gpt/ab_userdata/slot-post-process.sh"
    replace_once(
        slot_post,
        "   BOOT)\n      sed -i",
        "   BOOT)\n      cat > \"$IMAGEMOUNTPATH/slot.map\" <<'EOF'\n"
        "a.boot=::2\na.system=::5\nb.boot=::3\nb.system=::6\nEOF\n"
        "      sed -i")

    rules = output / (
        "image/gpt/ab_userdata/device/rootfs-overlay/etc/udev/rules.d/"
        "99-rpi-05-image.rules")
    rules.write_text(
        'SUBSYSTEM=="block", ENV{RPI_ONBOOTDEV}=="1", '
        'ENV{ID_PART_ENTRY_NUMBER}=="1", SYMLINK+="disk/by-slot/bootconfig"\n'
        'SUBSYSTEM=="block", ENV{RPI_ONBOOTDEV}=="1", '
        'ENV{ID_PART_ENTRY_NUMBER}=="7", SYMLINK+="disk/by-slot/persistent"\n',
        encoding="utf-8")

    generator = output / (
        "image/gpt/ab_userdata/device/rootfs-overlay/usr/lib/systemd/"
        "system-generators/slot-perst-generator")
    replace_once(
        generator,
        '  PARTLABEL="$(blkid -s PARTLABEL -o value -- "$ROOT_DEV" 2>/dev/null || true)"\n'
        '  case "$(printf \'%s\' "$PARTLABEL" | tr \'[:upper:]\' \'[:lower:]\')" in\n'
        '    system_a) SLOT="system_a" ;;\n'
        '    system_b) SLOT="system_b" ;;\n'
        '  esac',
        '  case "$ROOT_DEV" in\n'
        '    *p5) SLOT="system_a" ;;\n'
        '    *p6) SLOT="system_b" ;;\n'
        '  esac')

    # The upstream IDP format cannot represent DOS extended/logical
    # partitions. The image itself is already validated by genimage/sfdisk;
    # omit only the unsupported optional provisioning document for this MBR
    # layout and verify the partition table with our independent checker.
    post_image = output / "image/post-image.sh"
    replace_once(
        post_image,
        'if [ "${IGconf_image_provider:-}" = "genimage" ] && [ -f ${1}/genimage.cfg ] ; then',
        'if [ "${IGconf_device_class:-}" = "zero2w" ] ; then\n'
        '   warn "IDP: omitted because its schema cannot represent DOS logical partitions."\n'
        'elif [ "${IGconf_image_provider:-}" = "genimage" ] && [ -f ${1}/genimage.cfg ] ; then')
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(prepare(args.upstream, args.output))


if __name__ == "__main__":
    main()
