# Millennium continuation handoff

The authoritative objective is to address every unchecked item in
[`TODO.md`](TODO.md). Do not treat a passing QEMU run as proof of a physical
Zero 2 W, radio, USB, audio, power, or first-time-user requirement.

## Current repository state

- Working branch: `fix/startup-segv-no-serial`.
- The production image source commit is `c0427f2`; subsequent commits harden
  QEMU and begin the lifetime downloadable-experience system.
- `387c87c` contains the completed downloadable-experience subsystem plus the
  physical-image fixes for `wpasupplicant` and the monitor collector directory;
  `6b2c366` records the rejected `2ad5179` physical boot. Both are pushed to
  `origin/main`. Use `6b2c366` as the exact source identity for the next image.
- Latest completed downloadable-experience commits:
  - `f53b54c` — generated package/catalog schemas and compatibility metadata;
  - `d35f4fa` — deterministic schema-2 packages, bounded extraction, exact file
    inventory, compatibility checks, and persistent package anti-rollback;
  - `40c7df7` — deterministic signed catalog creation and local rollout,
    hold/group/withdrawal/denylist eligibility.
- QEMU hardening is in `13b62a6`; the prior full exact-image evidence was
  generated from the production image and passed all 18 artifact hashes.
- The final local software audit on 2026-09-07 passed 151 host unit tests, 6
  web-authorization tests, 2 owner-boundary tests, 50 content/story/security
  tests, 68 tools tests, every simulator scenario, 16 OTA tests, generated
  schema/version checks, and `git diff --check`.
- The 2026-09-07 plugged-media rerun exposed and fixed two QEMU portability
  defects: initramfs `/run` discarded the exact-image udev aliases before
  systemd consumed them, and Linux does not guarantee the numeric
  `/dev/vportNpM` name. The exact-image harness now injects configuration only
  into its disposable overlay, and the portable lab uses the stable
  `/dev/virtio-ports/millennium.mcu` link. The signed-experience lifecycle also
  establishes the same idle handset state required by the production worker.
- At `2026-09-07T19:13:20Z`, FIDO access to `anima` remained healthy but its
  loopback phone listener `127.0.0.1:22022` was absent; do not infer live-phone
  state until the reverse tunnel returns.

Large QEMU and image artifacts intentionally live outside the repository at
`/Volumes/UEBuild/millennium-exact-image-qemu/`. The most recent complete
evidence bundle is
`goal-state-20260907T1919Z/artifacts/full-20260907T201143Z/`. Its
`full-test-result.json` reports a pass with exact production-image userspace
tested and `physical_hardware_claimed: false`.

## Immediate execution order

1. Build, sign, factory-seed, write, fully read back, and physically boot a new
   phone-001 recovery candidate from exact source `6b2c366`. The `2ad5179`
   card booted but is rejected: it omitted `wpasupplicant` and the monitor
   collector directory. Before rerunning health, attach both Arduino MCUs and
   provide the phone a route with working DNS. Exact rejection evidence is in
   `hardware/as-built/phone-001/evidence/physical-boot-rejection-2ad5179-2026-09-07.json`.
2. Publish the immutable release objects and signed stable/beta/device-group
   catalogs with `content/publish_catalog.py` after completing the offline
   content/catalog signing ceremony. FIDO-authenticated access to `anima` is
   working through `/opt/homebrew/bin/ssh`; its operator-writable update root is
   `/home/kmatzen/selfhosted/millennium-updates/www`, bind-mounted read-only into
   the `millennium-updates` nginx container.
   `updates.kmatzen.com` is transport, not trust; signatures remain mandatory.
3. When the assembled phone and operator are available, follow
   [`hardware/as-built/phone-001/NEXT_PHYSICAL_SESSION.md`](hardware/as-built/phone-001/NEXT_PHYSICAL_SESSION.md)
   exactly. That single session is intended to close the remaining P0 physical
   Wi-Fi, A/B OS, power-interruption, recovery-media, external-maintenance,
   as-built, and first-time-caller evidence gaps.

## Required verification

For content/catalog changes, run:

```sh
python3 -m unittest content.tests.test_catalogtool \
  content.tests.test_storytool content.tests.test_content_install \
  content.tests.test_experience_agent content.tests.test_publish_catalog
python3 -m unittest discover -s tools/tests -p 'test_*.py'
python3 tools/generate_experience_schemas.py --check
python3 tools/generate_version_metadata.py --check
git diff --check
```

For full appliance changes, use the external-backed QEMU state and exact image:

```sh
export MILLENNIUM_QEMU_STATE=/Volumes/UEBuild/millennium-exact-image-qemu/goal-state-20260907T1919Z
export MILLENNIUM_QEMU_SSH_PORT=2234
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
