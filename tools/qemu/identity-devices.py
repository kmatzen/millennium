#!/usr/bin/env python3
"""Expose two firmware identity endpoints as stable QEMU pseudo-serial ports."""

import argparse
import json
import os
import pathlib
import pty
import select


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--release", required=True)
    parser.add_argument("--directory", default="/run/millennium-mcu")
    args = parser.parse_args()
    firmware = json.loads(pathlib.Path(args.release).read_text())["firmware"]
    root = pathlib.Path(args.directory)
    root.mkdir(parents=True, exist_ok=True)
    endpoints = {}
    for role in ("keypad", "display"):
        master, slave = pty.openpty()
        os.set_blocking(master, False)
        link = root / role
        link.unlink(missing_ok=True)
        link.symlink_to(os.ttyname(slave))
        endpoints[master] = (slave, firmware[role])
    while True:
        readable, _, _ = select.select(list(endpoints), [], [])
        for master in readable:
            try:
                query = os.read(master, 512)
            except OSError:
                continue
            if not query:
                continue
            identity = endpoints[master][1]
            expected = b"I" if identity["role"] == "keypad" else b"\x07"
            if expected in query:
                response = ("MILLENNIUM role={role} version={version} protocol={protocol} "
                            "build={build} selftest=ok\n").format(**identity).encode()
                os.write(master, response)


if __name__ == "__main__":
    main()
