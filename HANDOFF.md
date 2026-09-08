# Millennium continuation handoff

The authoritative objective is to address every unchecked item in
[`TODO.md`](TODO.md). Do not treat a passing QEMU run as proof of a physical
Zero 2 W, radio, USB, audio, power, or first-time-user requirement.

## Current repository state

- Working branch: `main`.
- The current production-image source is
  `6f50d127e5813e583e6898a6986175614422470a`; it includes rebuilt Alpha and
  Beta firmware plus observable MCU/I2C/health semantics and is pushed to
  `origin/main`.
- The factory-seeded candidate is signed, independently verified, retained on
  `anima`, and copied directly to external `UEBuild`. Its compressed SHA-256 is
  `4509d94f76d12056dbad1d2c92c7281cd8924c1375814cc05d6b9696d5f55efd`;
  its expanded SHA-256 is
  `f5c6bd4954374b0f06e206c748f096eb24964f767de0fff8d6036ce4790962b8`.
- The complete 17-layer QEMU software lab passed at
  `2026-09-08T08:57:35Z`, including the exact factory image's userspace,
  external captive-portal client, OTA origin and failures, maintenance tunnel,
  virtual MCU/peripheral faults, A/B recovery, experiences, and abrupt power.
  Exact evidence is in
  `hardware/as-built/phone-001/evidence/zero2w-recovery-seeded-artifact-6f50d12-2026-09-08.json`.
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

Large QEMU and image artifacts intentionally live on `anima` and external
`UEBuild`, not laptop internal storage. The latest complete QEMU evidence is
`/data2/millennium-build-5d7bdda/repo/tools/qemu/state/artifacts/full-20260908T083945Z/`
on `anima`; its `full-test-result.json` records a pass with exact production
image userspace tested and `physical_hardware_claimed: false`.

## Immediate execution order

1. Write the retained `6f50d12` phone-001 artifact to an exactly identified
   dedicated removable device, verify its complete image-length readback, then
   physically boot it. Do not reuse prior-candidate readback as evidence. Both
   Arduino MCUs are required for the physical health gate; phone peripherals
   may be absent only for explicitly scoped platform validation.
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

For full appliance changes, run the retained exact image on `anima`:

```sh
/opt/homebrew/bin/ssh anima \
  'docker exec \
    -e MILLENNIUM_QEMU_EXACT_IMAGE=/image/exact-6f50d12/phone001-6f50d12.img \
    -e MILLENNIUM_QEMU_EXACT_KERNEL=/workspace/tools/qemu/state/exact-vmlinuz \
    -e MILLENNIUM_QEMU_EXACT_INITRD=/workspace/tools/qemu/state/exact-initrd \
    millennium-qemu-e2e-8ac303b tools/qemu/qemu.sh full-test'
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
