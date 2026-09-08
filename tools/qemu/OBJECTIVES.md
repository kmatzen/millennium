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

Acceptance: `tools/qemu/qemu.sh ota-test`,
`tools/qemu/qemu.sh ota-fault-test`, and
`tools/qemu/qemu.sh ota-external-origin-test`. The external-origin test moves
the signed artifacts across the VM boundary, stops the guest-local fixture,
and proves TLS identity, transfer cutoff, health rollback, stale/withdrawn
manifest, service recovery, and link invariants against an independently
hosted HTTPS endpoint.

## Full operating-system OTA state machine

- [x] Exercise atomic HTTPS staging, interrupted and truncated downloads,
  inactive boot/root writes, readback, and pre-reboot selection in the ARM64
  guest.
- [x] Exercise candidate-health failure, unchanged normal-slot fallback,
  digest-scoped backoff/quarantine, explicit retry, successful health commit,
  and persistent anti-rollback sequence state.
- [x] Keep Raspberry Pi firmware selection, SD-card electrical behavior, and
  physical power cuts explicitly outside QEMU claims.

Acceptance: `tools/qemu/qemu.sh os-ota-test`.

This acceptance command also runs the production factory-seeding and
shared-mount-generator contract tests. It proves that every declared shared
path is linked into `local-fs.target`; it does not claim that the `virt` guest
booted Raspberry Pi firmware or the Zero 2 W disk image.

## Exact production-image boot

- [x] Boot the expanded, factory-seeded MBR A/B production image as the QEMU
  system disk rather than copying selected files into a generic guest.
- [x] Exercise its real system slot, persistent partition, shared-state
  generator, mount graph, service accounts, D-Bus and resolver startup.
- [x] Inject the `virt` machine's `/dev/vda*` slot identities only through
  ephemeral initramfs `/run` udev rules; never modify the image under test.
- [x] Fail on the directory-permission regressions observed on physical cards
  and retain the full serial transcript plus a machine-readable fidelity
  statement.
- [x] Require both FAT boot partitions, nftables, and the Millennium firewall;
  reject generic transport initramfs images that omit their kernel modules.

Acceptance: set `MILLENNIUM_QEMU_EXACT_IMAGE`,
`MILLENNIUM_QEMU_EXACT_KERNEL`, and `MILLENNIUM_QEMU_EXACT_INITRD`, then run
`tools/qemu/qemu.sh exact-image-test`.

This is exact for the disk bytes, partitioning, arm64 userspace and systemd
startup graph. It is not firmware or board emulation: QEMU `virt` uses a
generic transport kernel because no QEMU machine models the Zero 2 W's exact
BCM2710A1 board, VideoCore firmware, SD electrical path, Wi-Fi, USB topology or
power behavior. Those remain physical acceptance gates.

## Networking and onboarding

- [x] Provide a simulated NetworkManager/radio boundary for first boot, hidden
  SSIDs, WPA transition/open-network policy, wrong credentials, radio loss,
  atomic-save interruption, AP rollback, timeout, and recovery gesture.
- [x] Exercise captive-portal probe behavior for iOS, Android, macOS, and
  Windows HTTP probes from isolated test clients.
- [x] Prove setup clients cannot reach SSH, admin API, forwarding, or stored
  credentials while update and maintenance endpoints recover after success.
- [x] Establish the production reverse-SSH service against an external test
  server, traverse the forwarded port back into the guest, and prove it fails
  closed and reconnects after the server disappears and returns.

Acceptance: `tools/qemu/qemu.sh wifi-test` and
`tools/qemu/qemu.sh wifi-external-client-test`. The latter runs an independent
client outside the VM through QEMU's forwarded network boundary and exercises
the real portal server, session cookie, CSRF token, probe evidence, page
rendering, and helper handoff.

Acceptance: `tools/qemu/qemu.sh maintenance-external-tunnel-test` exercises the
maintenance transport against an SSH server outside the guest.

## Experiences and observability

- [x] Run the real story engine and physical input vocabulary through the VM.
- [x] Automatically traverse primary, interruption, timeout, repeat, invalid,
  offline, optional-input, and return-visit story paths through the VM boundary.
- [x] Export display/audio selection, peripheral state, story state, daemon
  journal, metrics, and fault timeline as one timestamped test artifact.
- [x] Install a newly signed data experience through a guest-local HTTPS
  catalog using the production phone-side lifecycle worker and QEMU-only keys.
- [x] Prove the lifecycle worker preserves its known-good release across a
  compromised catalog signature, HTTPS-origin loss, signed withdrawal, and a
  systemd worker restart in a repeatable reused-guest test.
- [x] Exercise real disk exhaustion in an isolated, size-bounded guest
  filesystem and retain its readable known-good fallback.
- [x] Exercise host-driven virtual power cuts at every content activation
  journal boundary.
- [x] Provide a single noninteractive full-lab command that starts or reuses the
  VM, provisions current source, runs every software acceptance layer, and emits
  a machine-readable summary with no false physical-hardware claims.

Acceptance: `tools/qemu/qemu.sh experience-test`,
`tools/qemu/qemu.sh experience-lifecycle-test`,
`tools/qemu/qemu.sh experience-power-test`, and
`tools/qemu/qemu.sh full-test`.

## Explicit non-objectives

The lab does not claim Raspberry Pi electrical fidelity, instruction-level
ATmega32U4 timing, native USB CDC enumeration, real Wi-Fi RF behavior, handset
audio quality, VFD legibility, coin-validator voltage timing, power-rail
brownout behavior, or human discoverability. Those remain physical gates.
