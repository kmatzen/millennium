# Fail-safe operating-system OTA

## Current device and migration boundary

`phone-001` is a Raspberry Pi Zero 2 W running 32-bit Raspberry Pi OS
Bullseye from a conventional two-partition card: one 256 MiB FAT boot
partition and one ext4 root partition. That layout has no inactive target and
cannot provide power-loss-safe full-OS replacement. Never repartition it over
SSH or through application OTA. Migration requires a separately prepared and
verified card while the operator has local access to the phone.

The target base is a supported Raspberry Pi OS Lite release where
NetworkManager is the standard network authority. It must include the Wi-Fi
onboarding stack and all package dependencies in the image; first boot must not
need upstream Internet.

## Boot architecture

Use the Raspberry Pi firmware's one-shot `tryboot` facility and partition-level
`tryboot_a_b=1` selection. The firmware clears the try flag before launching the
candidate, so a reset, kernel panic, watchdog expiry, or power loss before
commit returns to the normal slot on the next boot. Only a successful userspace
health gate may swap the normal and try slot selectors.

The immutable image layout must provide:

- a minimal FAT selector/boot-control area;
- independent boot A and boot B payload regions;
- independent read-only-by-policy root A and root B partitions;
- one persistent data partition for device identity, NetworkManager profiles,
  setup secret, maintenance keys and host identity, story state, operational
  logs, anti-rollback sequence, failure quarantine, and update journals.

Exact partition numbers, sizes, UUIDs, selector files, and mount units belong in
a generated layout manifest. The builder must reject overlap, a writable active
target, duplicate UUIDs, insufficient space, and any root configuration that
points at the other slot. Do not infer the active slot from symlink state; read
the boot partition selected by firmware from device tree and cross-check the
mounted root PARTUUID.

On BCM2837 devices such as Zero 2 W, BOOTCONFIG also contains the pinned
`bootcode.bin` and a zero-length `start.elf` bootability marker. The boot ROM
loads `bootcode.bin` from the first FAT partition; the marker allows that
second stage to process `autoboot.txt` and load the real firmware from the
selected A/B boot partition.

The MBR target cannot use the upstream GPT-label mapper. Its four-entry static
`slot.map` must be embedded in every initramfs at `/boot/slot.map`; the build
regenerates all initramfs images after installing the map and fails unless
archive inspection proves the file is present. Without that early-boot copy,
the missing active-root alias intentionally triggers the fail-safe reboot.
Immediately before the safety check, `89-millennium-mbr-slots` reads the
firmware-selected partition and creates the fixed MBR active/other aliases.
This removes dependence on GPT labels and early udev classification while
remaining fail-closed for any unexpected partition or missing block device.

Raspberry Pi documents `autoboot.txt`, `boot_partition`, `tryboot_a_b`, the
one-shot `reboot '0 tryboot'` flow, and the device-tree boot selection fields in
its official configuration reference:
<https://www.raspberrypi.com/documentation/computers/config_txt.html#autoboottxt>.

## Signed OS release

An OS release is separate from the application/content bundle. Its canonical
manifest must contain:

- schema, channel, key ID, monotonically increasing sequence, and publication
  time;
- supported board models and architecture;
- complete generated partition-layout identity;
- full source commit and reproducible image provenance;
- minimum compatible application, content, MCU, and persistent-state schema
  versions;
- compressed and uncompressed sizes plus SHA-256 for every boot and root
  payload;
- rollout groups, hold/withdrawal state, and minimum allowed OS sequence.

The offline release key signs the exact canonical manifest bytes. The device
downloads into persistent staging, verifies the signature and compressed hash,
streams decompression into the inactive slot while hashing the uncompressed
bytes, then reads the inactive slot back and verifies it before changing boot
state. It must never write the active boot or root partition.

The implemented manifest builder and validation primitives are:

```sh
python3 tools/build_os_release.py \
  --sequence 1 --version 2026.09.0 \
  --base-url https://updates.kmatzen.com/millennium/os \
  --boot-image boot.img --root-image root.img \
  --layout-id zero2w-ab-mbr-v1 \
  --board-model "Raspberry Pi Zero 2 W Rev 1.0" \
  --minimum-application-version 0.4.0 --minimum-mcu-version 0.4.0 \
  --persistent-state-schema 1 --private-key /run/keys/release.pem \
  --output-dir /tmp/millennium-os-release
```

`host/os_ota/millennium_os_ota.py` verifies the detached Ed25519 signature,
device/channel/rollout compatibility, monotonic sequence, and both compressed
and expanded payload sizes and hashes. The block-device writer and tryboot
transaction deliberately remain a separate gate: validating an image never
implies permission to select or write a device.

`host/os_ota/millennium_os_ota_agent.py` is the installed production
orchestrator. Separate systemd timers check and apply releases, while boot-time
recovery and health services reconcile interrupted writes, recognize firmware
fallback, and commit a one-shot candidate only after all nine checks pass. It
uses the image runtime's `/bootfs` mount and `/dev/disk/by-slot/{active,other}`
aliases, resolves inactive aliases before entering the symlink-rejecting block
writer, and derives direction from the current validated selector so both
A-to-B and B-to-A rotations use the same code path.

The same module now provides the inactive-slot write primitive. It validates
both downloads before the first write, rejects symlinks, rejects a boot/root
alias and any target that aliases an active device, requires real block devices
unless the explicit test-only regular-file mode is selected, hashes while
writing, calls `fsync`, reads back exactly the signed expanded length, and
persists each phase through an fsynced atomic journal. The final
`candidate-written` phase proves only that inactive bytes are durable; it does
not select tryboot or authorize a reboot.

Boot selection is implemented as a second, separately gated primitive. It
generates and strictly parses a sub-512-byte `autoboot.txt` containing only
`tryboot_a_b` and the normal/candidate partition numbers, writes and reads it
back atomically, journals `tryboot-armed`, and invokes `reboot "0 tryboot"` as
a fixed argument vector. Commit requires all nine health checks, reads both the
actual boot partition and tryboot flag from firmware device-tree properties,
and refuses to swap the selector unless they identify the expected one-shot
candidate boot. Without that commit, Raspberry Pi firmware clears the try flag
and returns to the unchanged normal partition on the next reset.

OS rollout policy is enforced independently of image validity. Installation is
deferred whenever a call, coin transaction, content save, maintenance session,
or another update is active and outside the configured maintenance window.
Failures are keyed by canonical manifest SHA-256, receive exponential retry
delay, and become quarantined after the configured attempt count. A privileged
maintenance action may explicitly clear one manifest's failure record.
Owner-facing status reports only release, phase, retry timing, and whether
maintenance is required; it omits device paths, URLs, detailed errors, and
credentials.

A candidate health failure durably records every check and the failed subset,
updates the digest-scoped retry record, and leaves the normal selector
unchanged. A successful candidate commit writes the installed OS sequence to
persistent state before journaling completion, preserving anti-rollback state
across root-slot replacement.

## Transaction and boot commit

1. Refuse while a call, coin transaction, content save, or another updater is
   active, and honor the maintenance window.
2. Persist a signed-manifest digest and transaction journal before writing.
3. Write and read-verify inactive root and boot payloads.
4. Persist `candidate-written`, sync all affected block devices, and request a
   one-shot try boot.
5. On candidate boot, mount persistent state without migration that prevents
   the old slot from starting. Any schema upgrade must be backward-readable or
   copy-on-write until commit.
6. Within a bounded deadline require local filesystems, daemon, both MCU
   identities, audio, SIP, controls, signed update reachability, and the reverse
   maintenance tunnel. A watchdog must reset a wedged candidate.
7. If healthy, atomically swap the normal/try selectors, persist the installed
   OS sequence, and mark the journal committed. Otherwise reboot normally;
   one-shot try state is already cleared, so the prior slot returns.
8. The prior slot reconciles an uncommitted journal, records the failure by
   manifest digest, and quarantines it under the same retry policy as
   application OTA.

The runtime gate does not infer these checks from device nodes alone. It opens
the ALSA PCM path with a bounded silent sample; requires named daemon serial and
SIP health checks; waits for fresh, zero-drop diagnostics independently emitted
by Alpha and Beta; accepts only a valid daemon state; and requires the SSH
reverse-tunnel process to remain active longer than its 90-second server-alive
failure window. The current signed application release establishes each MCU's
exact role/build identity before OS activation; the per-board diagnostics prove
that both attested controllers are alive after the candidate boots.

## Persistent state contract

`host/os_ota/persistent-state.json` is the versioned allowlist for data that
survives a root-slot replacement. It includes the machine and SSH host
identities, Millennium configuration and setup secret, NetworkManager profiles,
the maintainer's SSH identity directory, application/story/OTA state, and
diagnostic logs. It intentionally excludes `/opt/millennium`, `/usr`, `/boot`,
and other replaceable software trees.

Generate the image's mount and creation fragments with:

```sh
python3 host/os_ota/persistent_state.py \
  host/os_ota/persistent-state.json \
  --fstab build/os-image/fstab.millennium \
  --tmpfiles build/os-image/millennium-persist.conf
```

The generator requires the exact identity/state target set, absolute normalized
mount targets, relative non-traversing persistent sources, unique non-overlapping
paths, fixed entry kinds, numeric owners, and restrictive modes for private
keys, Wi-Fi profiles, and maintainer SSH files. Both root slots receive the
same generated bind-mount fragment and empty target paths. The factory image
builder seeds unique identities and credentials into `PERSISTENT`; the
slot images never contain those values.

Seed a newly formatted, otherwise empty data filesystem from an offline staging
root, then independently verify its content inventory before image release:

```sh
sudo python3 host/os_ota/persistent_state.py \
  host/os_ota/persistent-state.json \
  --seed-from /mnt/device-staging --persistent-root /mnt/millennium-data
sudo python3 host/os_ota/persistent_state.py \
  host/os_ota/persistent-state.json \
  --verify-root /mnt/millennium-data
```

Seeding copies only contract-allowlisted targets, rejects a nonempty destination,
escaping links and special files, preserves numeric ownership throughout each
tree, reapplies declared top-level modes/owners, and records a mode-, ownership-,
and content-bound SHA-256 inventory. Verification fails if any entry, type,
permission, ownership, or byte changes after staging.

## Verification plan

Software tests must model torn downloads, decompression errors, short writes,
hash mismatches, selector-write loss, every journal boundary, watchdog reset,
candidate health failure, backward-compatible state, and both A-to-B and
B-to-A updates. QEMU may validate image construction, block writes, journal
recovery, and userspace gates, but it cannot prove Raspberry Pi firmware
`tryboot` behavior.

Physical acceptance uses a production-equivalent Zero 2 W and measured power
cuts during download, each inactive partition write, selector update, first
candidate boot, health evaluation, and commit. Every cut must return either to
the unchanged old slot or to a fully verified new slot—never an ambiguous or
unbootable state. Only after that matrix passes may `phone-001` be migrated by
swapping in the prepared recovery card and restoring its allowlisted
per-device state.

## Whole-disk recovery artifact

Retain the compressed whole-disk image separately from the normal boot/root OTA
payloads. Create its canonical manifest with `tools/build_recovery_image.py`.
The manifest binds the compressed and expanded sizes and SHA-256 digests, full
source commit, signing-key ID, supported board model, architecture, exact MBR
partition sizes, and A/B slot map. Sign `recovery-manifest.json` with the active
offline release key and retain `recovery-manifest.json.sig` beside the image.

Before writing recovery media, verify the signature with `openssl pkeyutl
-verify -rawin -pubin`, verify the compressed digest, run `zstd -t`, stream the
expanded image to the OS-identified removable whole disk, flush it, read the
complete disk back, and compare the expanded digest. Never infer the target
from its current disk number alone and never write an internal disk. Restore
only the allowlisted per-device state after the image and partition contract
have passed independent verification.

`tools/write_recovery_media.py` enforces that procedure. Run it first without
`--write` to verify the signed artifact and inspect the OS-reported target. To
write, rerun as root with `--write` and `--confirm-device` set to the exact
device identifier printed by the preflight. The command accepts only an
explicit removable whole disk, never a partition; rejects internal, undersized,
read-only, virtual, running-system, and source-image targets; verifies the
canonical manifest signature, compressed hash, and Zstandard stream; flushes
the expanded image; and hashes the complete image-length readback. Use
`--evidence` to retain the machine-readable result.

```bash
python3 tools/write_recovery_media.py \
  --manifest /path/to/recovery-manifest.json \
  --signature /path/to/recovery-manifest.json.sig \
  --public-key /path/to/update-signing-key.pem \
  --image /path/to/millennium-zero2w-ab.img.zst \
  --target /dev/diskN

sudo python3 tools/write_recovery_media.py \
  --manifest /path/to/recovery-manifest.json \
  --signature /path/to/recovery-manifest.json.sig \
  --public-key /path/to/update-signing-key.pem \
  --image /path/to/millennium-zero2w-ab.img.zst \
  --target /dev/diskN --write --confirm-device diskN \
  --evidence /path/to/recovery-media-write.json
```
