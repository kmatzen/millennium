#!/usr/bin/env python3
"""Verify the Millennium Zero 2 W A/B image's MBR partition contract."""

import argparse
from dataclasses import dataclass
import fcntl
import hashlib
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile


SECTOR = 512
EXTENDED_TYPES = {0x05, 0x0F, 0x85}
DARWIN_BLOCK_SIZE = 0x40046418
DARWIN_BLOCK_COUNT = 0x40086419
LINUX_BLOCK_SIZE_BYTES = 0x80081272


@dataclass(frozen=True)
class Partition:
    number: int
    kind: int
    start: int
    sectors: int

    @property
    def end(self):
        return self.start + self.sectors


def entries(sector):
    if len(sector) != SECTOR or sector[510:512] != b"\x55\xaa":
        raise ValueError("missing MBR signature")
    result = []
    for offset in range(446, 510, 16):
        kind = sector[offset + 4]
        start, size = struct.unpack_from("<II", sector, offset + 8)
        result.append((kind, start, size))
    return result


def read_partitions(path):
    with path.open("rb") as image:
        primary_entries = entries(image.read(SECTOR))
        partitions = [
            Partition(i, kind, start, size)
            for i, (kind, start, size) in enumerate(primary_entries, 1)
            if kind and size
        ]
        extended = next((p for p in partitions if p.kind in EXTENDED_TYPES), None)
        if extended is None:
            raise ValueError("missing extended partition")
        ebr_sector = extended.start
        number = 5
        seen = set()
        while ebr_sector:
            if ebr_sector in seen:
                raise ValueError("cyclic EBR chain")
            seen.add(ebr_sector)
            image.seek(ebr_sector * SECTOR)
            logical, link, *_ = entries(image.read(SECTOR))
            kind, relative_start, size = logical
            if not kind or not size:
                raise ValueError("empty logical partition entry")
            partitions.append(
                Partition(number, kind, ebr_sector + relative_start, size))
            number += 1
            link_kind, link_start, link_size = link
            if not link_kind and not link_start and not link_size:
                break
            if link_kind not in EXTENDED_TYPES or not link_start or not link_size:
                raise ValueError("invalid EBR link")
            ebr_sector = extended.start + link_start
    return sorted(partitions, key=lambda part: part.number)


def media_size(path):
    size = path.stat().st_size
    if size:
        return size
    with path.open("rb", buffering=0) as image:
        if sys.platform == "darwin":
            block_size = struct.unpack("I", fcntl.ioctl(
                image.fileno(), DARWIN_BLOCK_SIZE, bytes(4)))[0]
            block_count = struct.unpack("Q", fcntl.ioctl(
                image.fileno(), DARWIN_BLOCK_COUNT, bytes(8)))[0]
            return block_size * block_count
        if sys.platform.startswith("linux"):
            return struct.unpack("Q", fcntl.ioctl(
                image.fileno(), LINUX_BLOCK_SIZE_BYTES, bytes(8)))[0]
    raise ValueError("cannot determine block-device size")


def verify(path):
    partitions = read_partitions(path)
    expected_types = [0x0C, 0x0C, 0x0C, 0x0F, 0x83, 0x83, 0x83]
    if [part.number for part in partitions] != list(range(1, 8)):
        raise ValueError("expected partitions 1 through 7")
    if [part.kind for part in partitions] != expected_types:
        got = [f"0x{part.kind:02x}" for part in partitions]
        raise ValueError(f"unexpected partition types: {got}")
    data = [part for part in partitions if part.number != 4]
    for left, right in zip(data, data[1:]):
        if left.end > right.start:
            raise ValueError(f"partitions {left.number} and {right.number} overlap")
    if partitions[3].start > partitions[4].start or partitions[3].end < partitions[-1].end:
        raise ValueError("logical partitions are outside the extended container")
    image_sectors = media_size(path) // SECTOR
    if partitions[-1].end > image_sectors:
        raise ValueError("partition extends beyond the image")
    for part in partitions:
        size_mib = part.sectors * SECTOR / 1024 / 1024
        print(f"p{part.number}: type=0x{part.kind:02x} start={part.start} size={size_mib:.1f} MiB")
    print("Zero 2 W A/B MBR contract: PASS")


def regions_equal(path, left, right, length, chunk_size=1024 * 1024):
    """Compare two non-overlapping image regions without loading them whole."""
    with path.open("rb") as image:
        remaining = length
        offset = 0
        while remaining:
            size = min(chunk_size, remaining)
            image.seek(left + offset)
            a = image.read(size)
            image.seek(right + offset)
            b = image.read(size)
            if len(a) != size or len(b) != size or a != b:
                return False
            remaining -= size
            offset += size
    return True


def fat_copy(image, partition, name, destination):
    tool = shutil.which("mcopy")
    if tool is None:
        raise ValueError("mcopy is required for --boot-content (install mtools)")
    offset = partition.start * SECTOR
    result = subprocess.run(
        [tool, "-n", "-i", f"{image}@@{offset}", f"::{name}", str(destination)],
        text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ValueError(f"p{partition.number} missing {name}: {result.stderr.strip()}")


def require_size(path, minimum):
    if path.stat().st_size < minimum:
        raise ValueError(f"{path.name} is empty or implausibly small")


def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def verify_boot_content(path):
    partitions = read_partitions(path)
    parts = {part.number: part for part in partitions}
    boot_bytes = parts[2].sectors * SECTOR
    if parts[3].sectors != parts[2].sectors or not regions_equal(
            path, parts[2].start * SECTOR, parts[3].start * SECTOR, boot_bytes):
        raise ValueError("boot A and boot B are not byte-identical")

    with tempfile.TemporaryDirectory(prefix="zero2w-boot-verify-") as directory:
        root = Path(directory)
        wanted = {
            1: ("autoboot.txt", "bootcode.bin", "start.elf"),
            2: ("bootcode.bin", "start.elf", "kernel8.img", "initramfs8",
                "bcm2710-rpi-zero-2-w.dtb", "config.txt", "cmdline.txt", "slot.map"),
        }
        copied = {}
        for number, names in wanted.items():
            for name in names:
                target = root / f"p{number}-{name}"
                fat_copy(path, parts[number], name, target)
                copied[number, name] = target

        for number in (1, 2):
            require_size(copied[number, "bootcode.bin"], 32 * 1024)
            require_size(copied[number, "start.elf"], 512 * 1024)
        for name, minimum in (("kernel8.img", 1024 * 1024),
                              ("initramfs8", 1024 * 1024),
                              ("bcm2710-rpi-zero-2-w.dtb", 1024)):
            require_size(copied[2, name], minimum)
        for name in ("bootcode.bin", "start.elf"):
            if digest(copied[1, name]) != digest(copied[2, name]):
                raise ValueError(f"BOOTCONFIG {name} differs from boot-slot firmware")

        autoboot = copied[1, "autoboot.txt"].read_text(encoding="ascii")
        expected = ("[all]\ntryboot_a_b=1\nboot_partition=2\n"
                    "[tryboot]\nboot_partition=3\n")
        if autoboot != expected:
            raise ValueError("unexpected initial autoboot.txt selector")
        cmdline = copied[2, "cmdline.txt"].read_text(encoding="ascii").strip()
        for item in ("root=/dev/disk/by-slot/active/system", "rootwait",
                     "console=serial0,115200"):
            if item not in cmdline.split():
                raise ValueError(f"cmdline.txt missing {item}")
        config = copied[2, "config.txt"].read_text(encoding="ascii")
        for item in ("auto_initramfs=1", "[pi02]", "enable_uart=1",
                     "uart_2ndstage=1"):
            if item not in config:
                raise ValueError(f"config.txt missing {item}")
        expected_map = "a.boot=::2\na.system=::5\nb.boot=::3\nb.system=::6\n"
        if copied[2, "slot.map"].read_text(encoding="ascii") != expected_map:
            raise ValueError("unexpected boot-slot map")

        lsinitramfs = shutil.which("lsinitramfs")
        if lsinitramfs is None:
            raise ValueError("lsinitramfs is required for --boot-content")
        result = subprocess.run(
            [lsinitramfs, str(copied[2, "initramfs8"])], text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode:
            raise ValueError(f"cannot inspect initramfs8: {result.stderr.strip()}")
        members = set(result.stdout.splitlines())
        required = {"boot/slot.map",
                    "scripts/local-premount/89-millennium-mbr-slots"}
        missing = sorted(required - members)
        if missing:
            raise ValueError("initramfs8 missing: " + ", ".join(missing))
    print("Zero 2 W raw boot-content contract: PASS")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--boot-content", action="store_true")
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    image = args.image.resolve()
    verify(image)
    if args.boot_content:
        verify_boot_content(image)


if __name__ == "__main__":
    main()
