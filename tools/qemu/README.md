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

The lab has two complementary machines. The everyday cloud guest supports fast
development, OTA fault injection and peripheral co-simulation. The
`exact-image-test` boots the expanded factory image itself and exercises its
real MBR A/B layout, system slot, persistent partition, shared mounts and early
services. QEMU-only slot aliases are injected into `/run`; the tested image is
not altered. A physical Zero 2 W must still verify firmware selection,
read-only-root behavior, SD I/O, radios, USB, audio and power interruption.

## Requirements

- macOS arm64 or Linux
- QEMU with AArch64 and UEFI support (`brew install qemu` on macOS;
  `apt install qemu-system-arm qemu-efi-aarch64 qemu-utils` on Debian)
- Python 3, OpenSSH client and server, netcat, curl, tar, and socat
- about 2 GB RAM and 2 GB free disk space for the cloud lab; exact-image tests
  additionally require the expanded production image (currently about 15 GB)

Both the cloud guest and current production image use Debian arm64. The cloud
guest validates application, service, network, update, and protocol behavior;
the exact-image test additionally validates the assembled production userspace
and disk layout. Keep the real-device smoke test as a release gate.

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
tools/qemu/qemu.sh ota-test
tools/qemu/qemu.sh ota-fault-test
tools/qemu/qemu.sh ota-external-origin-test
tools/qemu/qemu.sh os-ota-test
tools/qemu/qemu.sh wifi-test
tools/qemu/qemu.sh maintenance-external-tunnel-test
tools/qemu/qemu.sh experience-test
tools/qemu/qemu.sh experience-lifecycle-test
tools/qemu/qemu.sh experience-power-test
tools/qemu/qemu.sh exact-image-test
tools/qemu/qemu.sh full-test
tools/qemu/qemu.sh stop
```

When all three exact-image variables shown below are set, `full-test` includes
the exact production-image gate and records
`exact_production_image_tested: true`. With none set it runs the portable cloud
lab and records `false`; a partial exact-image configuration is rejected. The
exact-image boot remains bounded at ten minutes because external-volume I/O and
concurrent emulation can make a valid first boot exceed five minutes.

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
peripheral state, OTA status/recovery/flash evidence, deterministic fault trace,
and a SHA-256 summary explicitly
marked `physical_hardware_claimed: false`.

Provisioning also installs the production OTA worker, timers, recovery unit,
immutable release layout, a guest-local HTTPS origin, and two independent MCU
identity endpoints. The lab creates its own disposable signing and TLS keys in
`/var/lib/millennium/qemu-ota`; these keys never leave the ignored VM overlay
and are not production credentials. `ota-test` signs and commits a release
through the real worker and attests both virtual MCU roles. `ota-fault-test`
exercises loss, corruption, withdrawal, quarantine, interrupted activation,
rollback, and active-link invariants.

`ota-external-origin-test` copies a newly signed release out of the guest,
serves it from an independent HTTPS process on the QEMU host, disables the
guest-local origin, and drives the production updater through apply, outage,
and recovery. It also injects wrong TLS identity, mid-bundle cutoff,
post-activation health loss, signed rollback and withdrawal, and exercises the
systemd update-check path used after Wi-Fi transition. This catches networking
and trust-boundary defects that an in-guest origin cannot expose.

`maintenance-external-tunnel-test` starts an isolated SSH server outside the
guest, provisions the production reverse-tunnel service against it, reaches
the guest only through the forwarded port, then proves fail-closed behavior
and automatic recovery across a server outage.

`os-ota-test` runs the full signed OS state machine inside the ARM64 guest. It
covers atomic HTTPS staging and interrupted/truncated downloads, inactive-slot
write/readback, pre-reboot selection, failed candidate fallback, digest-scoped
quarantine, health commit, persistent anti-rollback state, and both selector
directions using test-only virtual block files. Raspberry Pi firmware behavior
remains a physical Zero 2 W acceptance gate.

## Exact production-image test

Provide the expanded image and a generic arm64 QEMU `virt` kernel/initramfs.
The initramfs must carry the modules needed before the image's system slot is
mounted: `fat`, `vfat`, `nls_ascii`, `nls_cp437`, `nls_iso8859_1`, and
`nf_tables` (including their dependencies). A minimal cloud initramfs often
omits these because its own root disk does not need them; the exact-image test
will reject that transport rather than silently skipping production mounts or
firewall startup.

```sh
export MILLENNIUM_QEMU_EXACT_IMAGE=/path/to/millennium-zero2w-ab-phone-001.img
export MILLENNIUM_QEMU_EXACT_KERNEL=/path/to/vmlinuz-arm64
export MILLENNIUM_QEMU_EXACT_INITRD=/path/to/initrd.img-arm64
tools/qemu/qemu.sh exact-image-test
```

The harness creates disposable copy-on-write overlays and boots both immutable
system partitions (5 and 6),
injects `/dev/vda1`, `vda2`, `vda5`, and `vda7` as the production slot names
through ephemeral udev rules, and starts the image's own systemd. It requires
the persistent/shared mount graph, both FAT boot partitions, D-Bus, resolver,
nftables, the Millennium firewall, and the production daemon to start under
their real service accounts. It also executes the selected daemon binary as
the `millennium` account. It fails on the permission errors previously
observed on the physical image and writes `console.log` plus `result.json` under
`$MILLENNIUM_QEMU_STATE/exact-image/`.

The output explicitly records `raspberry_pi_firmware_emulated: false` and
`physical_hardware_claimed: false`. QEMU has no exact Zero 2 W machine, so this
test closes userspace/image-assembly gaps without pretending to validate the
VideoCore boot chain, BCM2710A1 peripherals, native SD/USB/Wi-Fi behavior or
brownouts.

`wifi-test` exercises the NetworkManager boundary through deterministic radio
faults, validates captive-portal behavior for the major platform probes, and
checks the setup firewall, service sandbox, atomic credential storage, rollback,
recovery gesture, timeout, and private factory handoff. RF behavior and real
client association remain physical gates.

`wifi-external-client-test` adds an outside-in transport check. A client process
outside QEMU crosses an explicit forwarded TCP boundary to the real portal in
the ARM64 guest, performs an Android captive probe, downloads acceptance
evidence, renders the setup form, preserves its session cookie, submits the
CSRF-protected form, and verifies the helper receives the intended candidate.
It does not claim RF association.

## Reset and recovery

```sh
tools/qemu/qemu.sh reset
tools/qemu/qemu.sh start
```

Reset is recoverable: the previous overlay is renamed with a timestamp instead
of deleted. The downloaded base image is retained. To change ports, state
location, or the size of a newly created overlay, set
`MILLENNIUM_QEMU_SSH_PORT`, `MILLENNIUM_QEMU_STATE`, or
`MILLENNIUM_QEMU_DISK_SIZE` (for example, `64G`). The size setting never
resizes an existing disk.

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

`qemu.sh full-test` reprovisions the current tree and runs every software layer
in dependency-safe order, including the production-paced offline story. It
emits a timestamped `full-test-result.json` whose
`physical_hardware_claimed` field is always false.

The QEMU lab complements rather than replaces these release gates:

1. host unit/scenario/content tests;
2. arm64 release compilation and signed OTA verification;
3. this ARM VM appliance smoke test;
4. a real phone smoke test for USB, audio, Wi-Fi, firmware, and peripherals.
