#!/usr/bin/env python3
"""Boot an exact Millennium A/B disk image and assert early appliance health.

QEMU's ``virt`` board cannot emulate a Zero 2 W.  This harness deliberately
uses a generic arm64 kernel/initramfs only as a transport for the exact image's
userspace, partition table, system slot, persistent slot and systemd graph.
It injects QEMU-only configuration in a disposable overlay; the source image
itself is never modified.
"""

from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
from pathlib import Path
import re
import select
import shutil
import socket
import subprocess
import tempfile
import time


PASS_MARKER = "MILLENNIUM_EXACT_IMAGE_PASS"
FAIL_MARKER = "MILLENNIUM_EXACT_IMAGE_FAIL"
DEFAULT_TIMEOUT_SECONDS = 600
FORBIDDEN = (
    "Failed to start dbus.service",
    "Failed to start systemd-resolved.service",
    "dbus-daemon: Permission denied",
    "systemd-resolved: Permission denied",
    "Failed to open /etc/systemd/resolved.conf: Permission denied",
    "Failed to mount boot-firmware.mount",
    "Failed to mount bootfs.mount",
    "Failed to start nftables.service",
    "Failed to start millennium-firewall.service",
)
ANSI_ESCAPE = re.compile(r"\x1b(?:\[[0-?]*[ -/]*[@-~]|\][^\x07]*(?:\x07|\x1b\\))")


def normalized_console(data: bytes | bytearray) -> str:
    """Remove terminal control sequences before matching boot evidence."""
    return ANSI_ESCAPE.sub("", data.decode(errors="replace")).replace("\r", "")


def has_marker(text: str, marker: str) -> bool:
    """Accept only a marker emitted as its own console line.

    systemd logs the complete ExecStart while parsing a generated unit. A
    substring check therefore accepted the marker embedded in configuration
    before the acceptance command had executed.
    """
    return any(line.strip() == marker for line in text.splitlines())


def require_program(name: str) -> str:
    path = shutil.which(name)
    if not path:
        raise SystemExit(f"missing required program: {name}")
    return path


def shell_commands(system_partition: int = 5) -> bytes:
    rules = (
        'KERNEL=="vda1", SYMLINK+="disk/by-slot/bootconfig"',
        'KERNEL=="vda2", SYMLINK+="disk/by-slot/active/boot"',
        f'KERNEL=="vda{system_partition}", SYMLINK+="disk/by-slot/active/system"',
        'KERNEL=="vda7", SYMLINK+="disk/by-slot/persistent"',
    )
    unit = (
        "[Unit]",
        "Description=QEMU exact-image acceptance",
        "Requires=dbus.service systemd-resolved.service NetworkManager.service daemon.service persistent.mount "
        "boot-firmware.mount bootfs.mount nftables.service "
        "millennium-firewall.service",
        "After=dbus.service systemd-resolved.service NetworkManager.service daemon.service persistent.mount "
        "boot-firmware.mount bootfs.mount nftables.service "
        "millennium-firewall.service local-fs.target",
        "[Service]",
        "Type=oneshot",
        "ExecStart=/bin/sh -xc 'exec >/dev/console 2>&1; "
        "systemctl is-active --quiet dbus.service && "
        "systemctl is-active --quiet systemd-resolved.service && "
        "systemctl is-active --quiet NetworkManager.service && "
        "systemctl is-active --quiet daemon.service && "
        "systemctl is-active --quiet boot-firmware.mount && "
        "systemctl is-active --quiet bootfs.mount && "
        "systemctl is-active --quiet nftables.service && "
        "systemctl is-active --quiet millennium-firewall.service && "
        "mountpoint -q /persistent && "
        "mountpoint -q /boot/firmware && "
        "mountpoint -q /bootfs && "
        "runuser -u messagebus -- test -x /usr/bin/dbus-daemon && "
        "runuser -u systemd-resolve -- test -r /etc/systemd/resolved.conf && "
        # Exercise the effective production ExecStart boundary. Testing only
        # base OS services allowed an untraversable /opt release to pass.
        "runuser -u millennium -- test -x "
        "/opt/millennium/current/host/millennium-daemon && "
        "runuser -u millennium -- "
        "/opt/millennium/current/host/millennium-daemon --version && "
        # Reproduce the physical dual-link case: the maintenance Ethernet
        # profile must never install a default route while owner Wi-Fi is the
        # upstream connection.  Check the assembled image, not just source.
        "test \"$(grep -Fxc never-default=true "
        "/etc/NetworkManager/system-connections/millennium-wired.nmconnection)\" "
        "-eq 2 && "
        "grep -Fqx no-auto-default=\\* "
        "/etc/NetworkManager/conf.d/10-millennium.conf && "
        "test \"$(nmcli -g ipv4.never-default connection show millennium-wired)\" "
        "= yes && "
        "test \"$(nmcli -g ipv6.never-default connection show millennium-wired)\" "
        "= yes && "
        # A successful Wi-Fi handoff must not fall back into an immortal
        # setup loop or leave the captive portal running indefinitely.
        "grep -Fqx Restart=no "
        "/etc/systemd/system/millennium-wifi-helper.service && "
        "grep -Fqx RuntimeMaxSec=900 "
        "/etc/systemd/system/millennium-wifi-helper.service && "
        "grep -Fqx BindsTo=millennium-wifi-helper.service "
        "/etc/systemd/system/millennium-wifi-portal.service && "
        # Validate the effective systemd identity boundary on the assembled
        # image.  Merely inspecting the unit text missed a real deployment
        # failure where the portal user could not traverse this directory.
        "test \"$(stat -c %U:%G:%a /run/millennium-wifi)\" "
        "= root:millennium-wifi:750 && "
        "runuser -u millennium-wifi -- test -x /run/millennium-wifi && "
        "test \"$(systemctl show -p Group --value "
        "millennium-wifi-bootstrap.service)\" = millennium-wifi && "
        # Octal-encode the suffixes so the serial echo of this injected unit
        # cannot itself contain either result marker.
        "printf \"MILLENNIUM_EXACT_IMAGE_\\120\\101\\123\\123\\n\" "
        ">/dev/console || "
        "printf \"MILLENNIUM_EXACT_IMAGE_\\106\\101\\111\\114\\n\" "
        ">/dev/console'",
    )
    quote = lambda value: "'" + value.replace("'", "'\\''") + "'"
    commands = [
        # /run belongs to the initramfs at this point and is replaced when
        # systemd starts.  Put the QEMU-only files in the disposable overlay
        # so they survive the switch to the real root filesystem.
        "mkdir -p /etc/udev/rules.d /etc/systemd/system/multi-user.target.wants",
        "printf '%s\\n' " + " ".join(map(quote, rules))
        + " > /etc/udev/rules.d/99-qemu-slot.rules",
        "printf '%s\\n' " + " ".join(map(quote, unit))
        + " > /etc/systemd/system/qemu-exact-accept.service",
        "ln -s ../qemu-exact-accept.service "
        "/etc/systemd/system/multi-user.target.wants/qemu-exact-accept.service",
        "exec /sbin/init",
    ]
    return ("\n".join(commands) + "\n").encode()


def connect_serial(path: Path, deadline: float) -> socket.socket:
    while time.monotonic() < deadline:
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            client.connect(str(path))
            client.setblocking(False)
            return client
        except (FileNotFoundError, ConnectionRefusedError):
            client.close()
            time.sleep(0.1)
    raise TimeoutError("QEMU serial socket did not become available")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--kernel", type=Path, required=True)
    parser.add_argument("--initrd", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_SECONDS)
    parser.add_argument("--system-partition", type=int, choices=(5, 6), default=5)
    args = parser.parse_args()
    for path in (args.image, args.kernel, args.initrd):
        if not path.is_file():
            raise SystemExit(f"missing input: {path}")
    args.output.mkdir(parents=True, exist_ok=True)

    qemu = require_program("qemu-system-aarch64")
    qemu_img = require_program("qemu-img")
    with tempfile.TemporaryDirectory(prefix="millennium-exact-qemu-") as raw:
        work = Path(raw)
        overlay = work / "overlay.qcow2"
        serial_path = work / "serial.sock"
        subprocess.run([
            qemu_img, "create", "-q", "-f", "qcow2", "-F", "raw",
            "-b", str(args.image.resolve()), str(overlay),
        ], check=True)
        command = [
            qemu, "-machine", "virt", "-cpu", "cortex-a72", "-smp", "4",
            "-m", "1024", "-kernel", str(args.kernel), "-initrd",
            str(args.initrd), "-append",
            f"root=/dev/vda{args.system_partition} rw console=ttyAMA0 init=/bin/sh",
            "-drive", f"file={overlay},if=none,id=disk0,format=qcow2",
            "-device", "virtio-blk-pci,drive=disk0", "-netdev",
            "user,id=net0", "-device", "virtio-net-pci,netdev=net0",
            "-display", "none", "-serial",
            f"unix:{serial_path},server=on,wait=off", "-monitor", "none",
        ]
        process = subprocess.Popen(command)
        deadline = time.monotonic() + args.timeout
        transcript = bytearray()
        result = "timeout"
        try:
            client = connect_serial(serial_path, deadline)
            with client:
                injected = False
                while time.monotonic() < deadline:
                    readable, _, _ = select.select([client], [], [], 0.25)
                    if readable:
                        chunk = client.recv(65536)
                        if not chunk:
                            break
                        transcript.extend(chunk)
                    text = normalized_console(transcript)
                    if not injected and "# " in text:
                        client.sendall(shell_commands(args.system_partition))
                        injected = True
                    if has_marker(text, PASS_MARKER):
                        result = "pass"
                        break
                    if has_marker(text, FAIL_MARKER) or any(item in text for item in FORBIDDEN):
                        result = "fail"
                        break
                    if process.poll() is not None:
                        result = "qemu-exited"
                        break
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

    log_path = args.output / "console.log"
    log_path.write_bytes(transcript)
    record = {
        "schema": 1,
        "result": result,
        "image": str(args.image.resolve()),
        "kernel": str(args.kernel.resolve()),
        "initrd": str(args.initrd.resolve()),
        "completed_at": datetime.now(timezone.utc).replace(
            microsecond=0).isoformat().replace("+00:00", "Z"),
        "qemu_machine": "virt",
        "exact_image_userspace": True,
        "image_size": args.image.stat().st_size,
        "system_partition": args.system_partition,
        "raspberry_pi_firmware_emulated": False,
        "physical_hardware_claimed": False,
        "console_log": str(log_path.resolve()),
    }
    (args.output / "result.json").write_text(
        json.dumps(record, indent=2, sort_keys=True) + "\n")
    print(json.dumps(record, sort_keys=True))
    return 0 if result == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
