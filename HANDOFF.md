# Millennium continuation handoff

The authoritative objective is to address every unchecked item in
[`TODO.md`](TODO.md). Do not treat a passing QEMU run as proof of a physical
Zero 2 W, radio, USB, audio, power, or first-time-user requirement.

## Current repository state

- Working branch: `fix/startup-segv-no-serial`.
- The production image source commit is `c0427f2`; subsequent commits harden
  QEMU and begin the lifetime downloadable-experience system.
- Latest completed downloadable-experience commits:
  - `f53b54c` — generated package/catalog schemas and compatibility metadata;
  - `d35f4fa` — deterministic schema-2 packages, bounded extraction, exact file
    inventory, compatibility checks, and persistent package anti-rollback;
  - `40c7df7` — deterministic signed catalog creation and local rollout,
    hold/group/withdrawal/denylist eligibility.
- QEMU hardening is in `13b62a6`; the prior full exact-image evidence was
  generated from the production image and passed all 18 artifact hashes.

Large QEMU and image artifacts intentionally live outside the repository at
`/Volumes/UEBuild/millennium-exact-image-qemu/`. The most recent complete
evidence bundle is
`full-state/artifacts/full-20260907T030556Z/`. Its
`full-test-result.json` reports a pass with exact production-image userspace
tested and `physical_hardware_claimed: false`.

## Immediate execution order

1. Continue the downloadable-experience Phase 2 work in
   [`host/docs/DOWNLOADABLE_EXPERIENCES.md`](host/docs/DOWNLOADABLE_EXPERIENCES.md):
   implement the phone-side signed-catalog poller, staged downloads, idle-only
   activation, durable activation journal, health rollback, digest quarantine,
   withdrawal/denylist handling, state migration, and bounded garbage
   collection. Reuse the existing OTA primitives and content installer rather
   than adding an unauthenticated updater.
2. Add owner dashboard status/enable/disable/fallback controls and
   privacy-preserving metrics, then extend unit and QEMU fault matrices across
   every durable transition.
3. Publish only after the catalog worker and compromise/failure tests pass.
   `updates.kmatzen.com` is transport, not trust; signatures remain mandatory.
4. When the assembled phone and operator are available, follow
   [`hardware/as-built/phone-001/NEXT_PHYSICAL_SESSION.md`](hardware/as-built/phone-001/NEXT_PHYSICAL_SESSION.md)
   exactly. That single session is intended to close the remaining P0 physical
   Wi-Fi, A/B OS, power-interruption, recovery-media, external-maintenance,
   as-built, and first-time-caller evidence gaps.

## Required verification

For content/catalog changes, run:

```sh
python3 -m unittest content.tests.test_catalogtool \
  content.tests.test_storytool content.tests.test_content_install
python3 -m unittest discover -s tools/tests -p 'test_*.py'
python3 tools/generate_experience_schemas.py --check
python3 tools/generate_version_metadata.py --check
git diff --check
```

For full appliance changes, use the external-backed QEMU state and exact image:

```sh
export MILLENNIUM_QEMU_STATE=/Volumes/UEBuild/millennium-exact-image-qemu/full-state
export MILLENNIUM_QEMU_EXACT_IMAGE=/Volumes/UEBuild/millennium-exact-image-qemu/phone001-c0427f2.img
export MILLENNIUM_QEMU_EXACT_KERNEL=/Volumes/UEBuild/millennium-exact-image-qemu/vmlinuz-6.1.0-52-cloud-arm64
export MILLENNIUM_QEMU_EXACT_INITRD=/Volumes/UEBuild/millennium-exact-image-qemu/initrd.img-6.1.0-52-cloud-arm64-millennium
tools/qemu/qemu.sh full-test
```

The custom transport initramfs includes FAT, charset, and nftables modules.
Using the older minimal cloud initramfs creates false boot-partition/firewall
failures and is not an equivalent test.

## Completion discipline

Only check off a TODO item when its scoped evidence exists. In particular,
automated captive-portal probes are not physical iOS/Android/macOS/Windows
tests; QEMU power cuts are not measured Zero 2 W power removal; a tunnel from
the home source network is not a genuinely external maintenance session; and a
scripted story traversal is not a first-time-caller playtest. Preserve failed
evidence and never relabel it as passing.
