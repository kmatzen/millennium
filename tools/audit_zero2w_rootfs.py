#!/usr/bin/env python3
"""Reject development and build residue in a Millennium production rootfs."""

import argparse
from pathlib import Path
import re


FORBIDDEN_PACKAGES = frozenset({
    "binutils", "binutils-aarch64-linux-gnu", "binutils-common", "build-essential",
    "clang", "cmake", "device-tree-compiler", "flashrom", "g++", "gcc", "git",
    "libbinutils", "make", "raspi-utils", "raspi-utils-dt", "raspi-utils-eeprom",
    "raspi-utils-otp", "raspinfo", "rpi-eeprom", "strace", "valgrind",
})

FORBIDDEN_BINARIES = (
    "usr/bin/as", "usr/bin/gcc", "usr/bin/git", "usr/bin/make", "usr/bin/objcopy",
    "usr/bin/dtc", "usr/sbin/flashrom",
)


def packages(path):
    result = set()
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        name = line.split("\t", 1)[0].split(":", 1)[0]
        if not re.fullmatch(r"[a-z0-9][a-z0-9+.-]*", name):
            raise ValueError("invalid package manifest entry")
        result.add(name)
    return result


def audit(root, manifest):
    root = Path(root)
    failures = []
    present = sorted(packages(manifest) & FORBIDDEN_PACKAGES)
    if present:
        failures.append("forbidden packages: " + ", ".join(present))
    binaries = [path for path in FORBIDDEN_BINARIES if (root / path).exists()]
    if binaries:
        failures.append("forbidden binaries: " + ", ".join(binaries))
    temporary = root / "tmp"
    if temporary.is_dir() and any(temporary.iterdir()):
        failures.append("target /tmp is not empty")
    apt_cache = root / "var/cache/apt/archives"
    if apt_cache.is_dir() and any(item.is_file() for item in apt_cache.rglob("*")):
        failures.append("apt package cache is not empty")
    application = root / "opt/millennium"
    if (application / ".git").exists() or any(application.rglob(".git")):
        failures.append("application VCS metadata is present")
    source_names = {"CMakeLists.txt", "Makefile"}
    source_suffixes = {".c", ".cc", ".cpp", ".h", ".hpp", ".o"}
    residue = [item.relative_to(root).as_posix() for item in application.rglob("*")
               if item.is_file() and
               (item.name in source_names or item.suffix in source_suffixes)]
    if residue:
        failures.append("application source/build residue: " + ", ".join(residue[:10]))
    if failures:
        raise ValueError("; ".join(failures))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--package-manifest", type=Path, required=True)
    args = parser.parse_args()
    try:
        audit(args.root, args.package_manifest)
    except (OSError, ValueError) as exc:
        raise SystemExit("production rootfs audit failed: %s" % exc)
    print("Production rootfs minimality: PASS")


if __name__ == "__main__":
    main()
