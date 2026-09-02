# Millennium QEMU appliance lab

This setup boots a complete Debian 12 ARM appliance, builds and runs the real
Millennium daemon under systemd, and connects it to a protocol-faithful virtual
display/keypad/card/hook/coin controller. It is intended for development,
integration tests, admin-dashboard work, release rehearsal, and failure testing.

It deliberately does **not** claim to emulate a Raspberry Pi Zero 2 W board.
QEMU's stable `virt` machine is used instead. USB enumeration, ALSA channel
routing, Wi-Fi radio/AP behavior, Arduino flashing, coin-validator electrical
timing, and the physical display still require a real-phone hardware test.

## Requirements

- macOS arm64 or Linux
- QEMU with AArch64 and UEFI support (`brew install qemu` on macOS;
  `apt install qemu-system-arm qemu-efi-aarch64 qemu-utils` on Debian)
- Python 3, OpenSSH, curl, tar, and socat
- about 2 GB RAM and 2 GB free disk space (the VM disk grows on demand)

The guest is Debian arm64. The Pi Zero 2 CPU supports AArch64, but the current
production image is armv7. QEMU therefore validates application, service,
network, update, and protocol behavior—not armv7 ABI compatibility. Keep the
existing armv7 release build and real-device smoke test as release gates.

## First boot

```sh
tools/qemu/qemu.sh start
tools/qemu/qemu.sh wait
tools/qemu/qemu.sh provision
tools/qemu/qemu.sh smoke
```

`start` downloads Debian's official generic-cloud image on first use, creates a
copy-on-write 16 GB overlay, generates a VM-only SSH key and NoCloud CIDATA
disk, starts the virtual MCU, and launches QEMU. All generated state remains
under `tools/qemu/state/` and is ignored by Git.

Cloud-init installs the native build dependencies. `provision` streams the
current working tree into the guest, compiles the daemon there, installs its
configuration and hardened systemd service, and starts it. Re-run `provision`
after source changes; rebuilding the disk is unnecessary.

## Everyday commands

```sh
tools/qemu/qemu.sh status
tools/qemu/qemu.sh ssh
tools/qemu/qemu.sh logs
tools/qemu/qemu.sh display
tools/qemu/qemu.sh hook up
tools/qemu/qemu.sh key 1
tools/qemu/qemu.sh coin 25
tools/qemu/qemu.sh card TEST-OWNER-TOKEN
tools/qemu/qemu.sh stop
```

To use the loopback-only dashboard and metrics endpoints, open an SSH tunnel:

```sh
tools/qemu/qemu.sh tunnel
```

Then visit `http://127.0.0.1:8081`. Obtain its bearer token with
`tools/qemu/qemu.sh token`. Metrics are at `http://127.0.0.1:8080`.

The virtual MCU writes each display update to `tools/qemu/state/display.json`
and logs all display and coin-control traffic in `tools/qemu/state/mcu.log`.
It implements framing, CRC-16/CCITT, HELLO negotiation, command ACKs, fragmented
input, resynchronization, sequence numbers, and periodic heartbeats.

## Reset and recovery

```sh
tools/qemu/qemu.sh reset
tools/qemu/qemu.sh start
```

Reset is recoverable: the previous overlay is renamed with a timestamp instead
of deleted. The downloaded base image is retained. To change ports or state
location, set `MILLENNIUM_QEMU_SSH_PORT` or `MILLENNIUM_QEMU_STATE`.

If boot fails, inspect `tools/qemu/state/console.log`. If the
daemon fails, use `qemu.sh logs` and `qemu.sh ssh systemctl status daemon`.

## Test layers

`python3 tools/qemu/test_virtual_mcu.py` runs without QEMU and checks protocol
framing. `qemu.sh smoke` checks the running guest, systemd service, virtio MCU
device, health API, metrics endpoint, input injection, and display response.

The QEMU lab complements rather than replaces these release gates:

1. host unit/scenario/content tests;
2. armv7 release compilation and signed OTA verification;
3. this ARM VM appliance smoke test;
4. a real phone smoke test for USB, audio, Wi-Fi, firmware, and peripherals.
