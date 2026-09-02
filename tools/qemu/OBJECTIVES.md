# QEMU completion objectives

This is the authoritative software-simulation ledger for the Millennium phone.
An objective is complete only when its named acceptance command passes. QEMU
evidence never substitutes for the physical acceptance rows in `TODO.md`.

## Appliance lifecycle

- [x] Boot a reproducible Debian 12 arm64 guest on QEMU `virt` with verified
  cloud-image provenance and a copy-on-write system disk.
- [x] Provision the real daemon from the current working tree as an enabled,
  hardened systemd service.
- [x] Expose SSH, loopback administration, health, and metrics without opening
  the administrative API to the LAN.
- [x] Provide deterministic graceful shutdown, abrupt power cut, restart,
  network disconnect/reconnect, pause/resume, and recoverable disk reset.
- [x] Preserve and restore named VM checkpoints for repeatable fault scenarios.

Acceptance: `tools/qemu/qemu.sh lifecycle-test` and
`tools/qemu/qemu.sh recovery-test`.

## Arduino and peripheral co-simulation

- [x] Preserve the Alpha → I2C → Beta → host topology rather than injecting
  events directly into daemon state.
- [x] Model keypad, hook, token reader, VFD, and coin acceptance/gate behavior.
- [x] Frame the host link with protocol version, length, type, sequence, CRC,
  negotiation, critical ACKs, fragmentation, and resynchronization.
- [x] Model serial-link loss/reconnect independently from I2C loss, delayed and
  dropped ACKs, CRC corruption, duplicate/replayed critical commands, MCU boot
  identity/reset diagnostics, watchdog resets, and validator program/verify
  failure.
- [x] Make virtual time and injected faults deterministic and persist a complete
  event trace suitable for CI diagnosis.

Acceptance: `python3 tools/qemu/test_virtual_mcu.py` and
`tools/qemu/qemu.sh peripheral-fault-test`.

## Signed OTA rehearsal

- [x] Install the production OTA worker, recovery service, timers, and immutable
  release layout in the guest.
- [x] Generate an ephemeral lab-only signing key and HTTPS update origin; never
  reuse or import the production private key.
- [x] Build, sign, publish, download, activate, health-check, and commit a full
  host/content/dual-MCU release through the production worker.
- [x] Rehearse manifest loss, bundle loss, corrupt signature/hash, withdrawn and
  quarantined releases, power loss during download/MCU flash/host activation,
  automatic recovery, and rollback to the previous signed release.
- [x] Prove an interrupted update never destroys `current` or `previous` and a
  recovered guest returns to a healthy daemon with independently attested MCU
  identities.

Acceptance: `tools/qemu/qemu.sh ota-test` and
`tools/qemu/qemu.sh ota-fault-test`.

## Networking and onboarding

- [x] Provide a simulated NetworkManager/radio boundary for first boot, hidden
  SSIDs, WPA transition/open-network policy, wrong credentials, radio loss,
  atomic-save interruption, AP rollback, timeout, and recovery gesture.
- [x] Exercise captive-portal probe behavior for iOS, Android, macOS, and
  Windows HTTP probes from isolated test clients.
- [x] Prove setup clients cannot reach SSH, admin API, forwarding, or stored
  credentials while update and maintenance endpoints recover after success.

Acceptance: `tools/qemu/qemu.sh wifi-test`.

## Experiences and observability

- [x] Run the real story engine and physical input vocabulary through the VM.
- [x] Automatically traverse primary, interruption, timeout, repeat, invalid,
  offline, optional-input, and return-visit story paths through the VM boundary.
- [x] Export display/audio selection, peripheral state, story state, daemon
  journal, metrics, and fault timeline as one timestamped test artifact.
- [x] Provide a single noninteractive full-lab command that starts or reuses the
  VM, provisions current source, runs every software acceptance layer, and emits
  a machine-readable summary with no false physical-hardware claims.

Acceptance: `tools/qemu/qemu.sh experience-test` and
`tools/qemu/qemu.sh full-test`.

## Explicit non-objectives

The lab does not claim Raspberry Pi electrical fidelity, instruction-level
ATmega32U4 timing, native USB CDC enumeration, real Wi-Fi RF behavior, handset
audio quality, VFD legibility, coin-validator voltage timing, power-rail
brownout behavior, or human discoverability. Those remain physical gates.
