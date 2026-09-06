#!/usr/bin/env python3
"""Verify the Millennium Zero 2 W A/B image's MBR partition contract."""

import argparse
from dataclasses import dataclass
from pathlib import Path
import struct


SECTOR = 512
EXTENDED_TYPES = {0x05, 0x0F, 0x85}


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
    image_sectors = path.stat().st_size // SECTOR
    if partitions[-1].end > image_sectors:
        raise ValueError("partition extends beyond the image")
    for part in partitions:
        size_mib = part.sectors * SECTOR / 1024 / 1024
        print(f"p{part.number}: type=0x{part.kind:02x} start={part.start} size={size_mib:.1f} MiB")
    print("Zero 2 W A/B MBR contract: PASS")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("image", type=Path)
    args = parser.parse_args()
    verify(args.image.resolve())


if __name__ == "__main__":
    main()
