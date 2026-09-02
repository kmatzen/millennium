# Millennium QEMU appliance lab

This setup boots a complete Debian 12 ARM appliance, builds and runs the real
Millennium daemon under systemd, and connects it to separate behavioral models
of the Alpha and Beta Arduinos plus their display, keypad, card, hook, and coin
peripherals. It is intended for development,
integration tests, admin-dashboard work, release rehearsal, and failure testing.

It deliberately does **not** claim to emulate a Raspberry Pi Zero 2 W board or
execute the AVR instructions. QEMU has no ATmega32U4 machine, and the physical
Beta link uses native USB CDC. The co-simulation instead preserves the real
Alpha → I2C → Beta → USB protocol boundary and Beta → peripheral routing. The
actual firmware ELFs remain covered by the reproducible Arduino build and must
pass the hardware release gate.

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
tools/qemu/qemu.sh peripherals
tools/qemu/qemu.sh fault i2c down
tools/qemu/qemu.sh fault i2c up
tools/qemu/qemu.sh fault coin jam
tools/qemu/qemu.sh fault coin clear
tools/qemu/qemu.sh fault serial down
tools/qemu/qemu.sh fault serial up
tools/qemu/qemu.sh fault ack drop
tools/qemu/qemu.sh fault ack delay 250
tools/qemu/qemu.sh fault crc next
tools/qemu/qemu.sh fault validator verify-fail
tools/qemu/qemu.sh fault validator verify-clear
tools/qemu/qemu.sh reset-mcu alpha
tools/qemu/qemu.sh reset-mcu beta
tools/qemu/qemu.sh reset-mcu watchdog-alpha
tools/qemu/qemu.sh reset-mcu watchdog-beta
tools/qemu/qemu.sh restart-virtual-mcu
tools/qemu/qemu.sh network down
tools/qemu/qemu.sh network up
tools/qemu/qemu.sh pause
tools/qemu/qemu.sh resume
tools/qemu/qemu.sh power-cut
tools/qemu/qemu.sh collect-artifacts my-test-run
tools/qemu/qemu.sh experience-test
tools/qemu/qemu.sh full-test
tools/qemu/qemu.sh stop
```

Cold disk checkpoints are available while the VM is stopped:

```sh
tools/qemu/qemu.sh checkpoint save known-good
tools/qemu/qemu.sh checkpoint list
tools/qemu/qemu.sh checkpoint load known-good
tools/qemu/qemu.sh checkpoint delete known-good
```

`power-cut` terminates QEMU without a guest shutdown specifically for recovery
testing. `reset` and checkpoint loading are recoverable but intentionally alter
the overlay selected for the next boot; do not keep irreplaceable data only in
the lab VM.

To use the loopback-only dashboard and metrics endpoints, open an SSH tunnel:

```sh
tools/qemu/qemu.sh tunnel
```

Then visit `http://127.0.0.1:8081`. Obtain its bearer token with
`tools/qemu/qemu.sh token`. Metrics are at `http://127.0.0.1:8080`.

The co-simulator writes the complete Arduino/peripheral state to
`tools/qemu/state/display.json` and logs VFD and validator traffic in
`tools/qemu/state/mcu.log`. Alpha models the 4x7 keypad, hook debounce boundary,
card reader, bounded I2C delivery, resets, and drop accounting. Beta models I2C
forwarding, USB framing, VFD writes, validator gate/reset/program state, resets,
and heartbeats. The shared link implements CRC-16/CCITT, HELLO negotiation,
critical-command ACKs, fragmented input, resynchronization, and sequences.
The status snapshot also carries a deterministic ticked trace, MCU firmware and
reset-cause identities, and every injected fault. Run
`qemu.sh peripheral-fault-test` for the live I2C, serial, validator, watchdog,
ACK, and CRC matrix.

`collect-artifacts NAME` creates `tools/qemu/state/artifacts/NAME/` with the
daemon journal, health JSON, Prometheus metrics, console/provision logs, VFD and
peripheral state, deterministic fault trace, and a SHA-256 summary explicitly
marked `physical_hardware_claimed: false`.

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
`qemu.sh lifecycle-test` verifies pause/resume and network isolation against an
already provisioned guest. `qemu.sh recovery-test` saves a named cold
checkpoint, boots it, cuts power without a guest shutdown, restores the
checkpoint, and requires a healthy daemon after reboot. The complete remaining objective ledger and exact
acceptance commands are in `OBJECTIVES.md`.

The QEMU lab complements rather than replaces these release gates:

1. host unit/scenario/content tests;
2. armv7 release compilation and signed OTA verification;
3. this ARM VM appliance smoke test;
4. a real phone smoke test for USB, audio, Wi-Fi, firmware, and peripherals.
