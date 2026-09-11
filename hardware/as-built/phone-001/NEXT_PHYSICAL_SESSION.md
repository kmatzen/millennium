# Phone 001 final physical acceptance session

This is the shortest safe path through every remaining hardware-dependent
gate. Keep the phone assembled and connected for the entire session; the order
below is intended to avoid another teardown or network rewire. None of these
steps may be replaced by QEMU evidence.

Before touching the phone, validate the coverage inventory and print the exact
remaining physical-only plan:

```bash
python3 tools/coverage_ledger.py validate
python3 tools/coverage_ledger.py physical-plan
```

Run the `host` and `contracts` ledger suites locally and the `qemu` suite on
`anima`. Do not begin this session unless all three pass for the candidate
source commit. The physical plan intentionally coalesces every remaining claim
into the single assembly, Wi-Fi-client, interruption, external-network, and
playtest phases below; it is the machine-checked guard against silently adding
an untested shipped function or repeating a hardware setup unnecessarily.

## Bring once

- the production-equivalent Raspberry Pi Zero 2 W;
- the controller board, Alpha and Beta Arduinos, display, keypad, handset,
  audio path, coin validator, and any token/card reader, all connected as they
  will be handed off;
- the production power supply plus a meter or oscilloscope capable of recording
  minimum 5 V rail voltage during boot, ringing, coin operation, and brownout;
- one dedicated, OS-reported removable SD card of at least 32 GB for recovery;
- two additional, physically distinct removable media for the new offline
  experience package/catalog signing keys (do not reuse the recovery card);
- the disconnected signing machine and both custodians' full OpenPGP hardware
  key fingerprints;
- the Mac with this repository and `UEBuild` attached;
- iOS, Android, macOS, and Windows clients;
- a cellular hotspot or other network that is genuinely outside the home ISP;
- two first-time callers who have not read the story documentation.

Do **not** use the 512 GB `UEBuild` volume as recovery media, regardless of the
`/dev/diskN` number macOS assigns it. It holds the builder, retained images, and
signing-key backup. Identify the recovery card by its reported media name,
capacity, and removable status immediately before writing it.

## 0. Establish and provision the experience trust roots

No production experience signing key currently exists in the repository,
local restricted storage, or `anima`'s retained signing vault. Before
publishing a catalog, generate separate package and catalog Ed25519 keys on the
disconnected signing machine. Export only their public keys. For each private
key, use `tools/signing_key_backup.py backup` for both custodians, copy the two
encrypted ciphertexts to distinct removable media with `copy-offline`, and run
an independent `recover` sign/verify drill from each read-only medium-backed
copy. Retain the evidence records and destroy the plaintext RAM-backed scratch
directory after each operation.

Add both public keys to the phone through an OTA release authenticated by the
existing `release-2026-08` trust root. Confirm their exact DER SHA-256 values
on the rebuilt phone before signing any production content. Never copy either
private key to this repository, the phone, `anima`, or the update web root.

After all content unit and QEMU gates pass, build and sign immutable packages
and stable/beta/device-group catalogs offline as described in
`content/README.md`. Transfer only the signed artifacts and public keys to a
private staging directory on `anima`, then run `content/publish_catalog.py`
against `/home/kmatzen/selfhosted/millennium-updates/www/experiences`. Verify
the public catalog, signature, and every catalog-bound immutable object before
enabling the phone's automatic catalog timer. The nginx container reads that
host directory through a read-only bind mount and does not require a restart.

## 1. Write and verify the corrected recovery card

**Current candidate `9a343aa` is the only candidate eligible for the next
write.** It supersedes `27b69be`, `7033d94`, and `c409237`. Physical boot of
`27b69be` proved the persistent namespace-root fix, read-only root, owner Wi-Fi,
correct Wi-Fi default route, and outbound maintenance tunnel, but exposed a
stale phone-001 factory overlay mapping `release-2026-08` to an absent public
key path. `9a343aa` rejects unresolved staged trust mappings before seeding and
tests the effective mappings and installed PEM on both exact image slots. Its
formal host, contracts, exact-image, and full QEMU release gates passed on
Anima. The signed expanded SHA-256 is
`c3d4efd4eb19bf1d88a02227792d56912e8b51315c1d81ee7cdb3815556ecbaa`;
the compressed SHA-256 is
`94123e2c561733925d55509d1881a33d2204cca91b351f7d6988114210ba8bc9`.
The verified package is retained at
`/Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-9a343aa-phone001-unapproved`.
Exact evidence is in
`evidence/zero2w-recovery-seeded-artifact-9a343aa-2026-09-11.json`.

The formerly empty production OS channel is no longer a pre-write blocker.
Signed baseline OS release `00000001-1.0.0` is published at the production
HTTPS origin. Its manifest signature, compatibility selection, compressed
objects, and complete 3,489,660,928-byte expansion were independently fetched
and verified from `anima` after publication. The expanded boot and root hashes
match the exact generic images used to build the qualified `9a343aa` recovery
candidate. Publication is fail-closed, immutable by release identity, and uses
the stable manifest rename as its commit point. Exact evidence is in
`evidence/os-release-00000001-1.0.0-production-2026-09-11.json`.

**The `27b69be` card is rejected and must not be redeployed.** Physical boot of
`c409237`
reached owner Wi-Fi and the maintenance tunnel but exposed missing persistent
state roots for both application and OS update checks. `27b69be` provisions
and exact-image-tests all production namespace roots: `ota`, `os-ota`, `hil`,
`backup`, and `physical-tests`. Both production system slots, the full QEMU
lab, host suite, contracts suite, and formal release gate passed on `anima`
without making a physical-hardware claim. The signed expanded SHA-256 is
`ea10c9f4c3e062c80f323535a0d1fdb9cc7138b66f5067edfae983f47dcfa55b`;
the compressed SHA-256 is
`114c7a83ae302778b5779c2045b7b281dbd88e919f0905949e5819b84060127e`.
The package is retained on `anima` at
`/data2/millennium-build-27b69be/recovery-package` and independently verified
on external storage at
`/Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-27b69be-phone001-unapproved`.
Exact evidence is in
`evidence/zero2w-recovery-seeded-artifact-27b69be-2026-09-11.json`.

**The signed `7033d94` package is superseded and must not be written.** It
fixed only the application OTA root; the later physical audit also exposed the
missing OS OTA root and showed that the remaining declared persistent roots
had not been provisioned or exercised.

**The `c409237` card is rejected and must not be redeployed.** Its physical
boot proved the corrected captive-portal firewall path, owner Wi-Fi
association, setup-AP shutdown, and outbound maintenance tunnel. It also
proved that the scheduled update check could not start because the production
image lacked `/var/lib/millennium/ota`; that defect is corrected and
regression-tested only in `7033d94` and later.

**The superseded `1f63772` candidate was built, factory-seeded, signed, and
accepted by its automated gates, but failed physical captive handoff and must
not be redeployed.** Its unchanged
application payload is from `4a15bf6`; the image-layer correction is
`1f63772`, and the final exact-image harness is `ff27782`. The exact seeded
image is retained on `anima` at
`/data2/millennium-build-1f63772/phone001-1f63772.img`; its expanded SHA-256 is
`aa1b2b0b1d5b312709666db22d6246ae0cfd921ba13cce595a59893ed5142e71`
and its size is 15,636,365,312 bytes. Full QEMU evidence is in
`/data2/millennium-build-1f63772/repo/tools/qemu/state/artifacts/full-20260909T171854Z/`.
Its compressed recovery artifact and canonical manifest are signed with the
active offline release key and independently verified on macOS, `anima`, and
the non-overwriting external `UEBuild` copy. All four hashes, the public-key
identity, signature, and complete expanded stream match without using laptop
internal storage. The dedicated 63.9 GB removable card was written on
2026-09-10, flushed, completely read back, matched signed expanded SHA-256
`aa1b2b0b1d5b312709666db22d6246ae0cfd921ba13cce595a59893ed5142e71`,
and safely ejected. Exact evidence is in
`evidence/zero2w-recovery-seeded-artifact-1f63772-2026-09-09.json` and
`evidence/recovery-media-phone001-1f63772.json`. Insert and boot this card; do
not use any predecessor card for acceptance.

**The `4a15bf6` card was written and fully read back, but its physical boot is
rejected.** `daemon.service` failed with systemd `203/EXEC` because the image
installed `/opt/millennium` as `root:root 0750`; the unprivileged `millennium`
service user could not traverse the executable path. The previous exact-image
test had accepted an echoed marker before its assertions ran. The `1f63772`
replacement fixes both the image mode and the gate. Evidence is in
`evidence/recovery-media-phone001-4a15bf6.json` and
`evidence/zero2w-recovery-seeded-artifact-1f63772-2026-09-09.json`. Do not
redeploy `4a15bf6`.

**The `98e018a` replacement card was written and fully verified, but its
physical boot is rejected.** Its full physical readback matched signed
expanded SHA-256
`d02b6ee091542cc0ee7158a3e46aa4f3078738b545e275933d91b14bd4a759a6`.
The exact seeded image and complete 17-layer QEMU lab passed. Evidence is in
`evidence/recovery-media-phone001-98e018a.json`. Physical testing found that
NetworkManager could report the setup AP active after brcmfmac stopped
beaconing; recovery did not cycle that stale connection. Build, simulate,
sign, write/read back, and boot a replacement from `6dad08e` or later. Exact
rejection evidence is in
`evidence/physical-boot-rejection-98e018a-2026-09-09.json`.

**The `d895b35` replacement card was written and fully verified, but its
physical boot is rejected.** Its complete readback matched signed expanded SHA-256
`01273e50f108d50f361672b58b9ccfac98340685bede42a61c895dd91b6c23b9`
and it was safely ejected. Evidence is in
`evidence/recovery-media-phone001-d895b35.json`. The exact seeded image and the
complete 17-layer QEMU lab passed, including an independent external captive-
portal client and maintenance/update channels. Physical tracing nevertheless
showed NetworkManager reached
station activation only after the four-second helper retry window expired.
Build, simulate, sign, write/read back, and boot a replacement from `b496081`
or later. Exact rejection evidence is in
`evidence/physical-boot-rejection-d895b35-2026-09-09.json`. The `c9b01fb` card is rejected because physical
testing exposed AP-to-owner activation timing and portal-lifetime defects fixed
by `dd8aa6e` and covered in the `d895b35` image.

**The `6f50d12` write is complete but its physical boot is rejected.** On 2026-09-08 macOS identified the
dedicated 63.9 GB USB-attached card as `/dev/disk6`, distinct from the 512 GB
`UEBuild` source. The writer reverified the signature, wrote and flushed all
15,636,365,312 bytes, and read the complete image length back. Readback matched
the signed expanded SHA-256 and the card was ejected. Evidence is retained in
`evidence/recovery-media-phone001-6f50d12.json`. Its physical boot exposed
defects corrected by `38350ac` and `03b5382`; see
`evidence/physical-boot-rejection-6f50d12-2026-09-08.json`. Build and verify a
replacement from `03b5382` or later before repeating this step. Prior media
writes remain failed/rejected evidence. The rejected image is retained on
`anima` at:

```text
/home/kmatzen/.local/share/millennium-recovery/images/zero2w-ab-1.0.0-6f50d12-phone001-unapproved
```

Its signed compressed SHA-256 is
`4509d94f76d12056dbad1d2c92c7281cd8924c1375814cc05d6b9696d5f55efd` and
its signed expanded SHA-256 is
`f5c6bd4954374b0f06e206c748f096eb24964f767de0fff8d6036ce4790962b8`.
The Ed25519 signature and Zstandard stream were independently verified on
`anima`; the signing operation and active public-key identity were also
verified on macOS. The factory-seeded image was copied directly from `anima`
to external `UEBuild` without using laptop internal storage. All four file
hashes, the manifest's source commit and expanded digest, the Ed25519
signature, and the Zstandard stream verified there. The complete 17-layer QEMU
lab also passed, including exact-image userspace. This does not replace a new
media write/readback or physical boot.

The rejected artifact remains named `unapproved` and must not be redeployed:

```text
/Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-6f50d12-phone001-unapproved
```

For a replacement write only, identify the dedicated card's current whole-disk
name with `diskutil list external physical`, verify the four recorded hashes
and signature, and run the preflight first:

```bash
python3 tools/write_recovery_media.py \
  --manifest /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-6f50d12-phone001-unapproved/recovery-manifest.json \
  --signature /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-6f50d12-phone001-unapproved/recovery-manifest.json.sig \
  --public-key /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-6f50d12-phone001-unapproved/update-signing-key.pem \
  --image /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-6f50d12-phone001-unapproved/millennium-zero2w-ab-phone-001.img.zst \
  --target /dev/diskN
```

Read the reported identity, confirm it is the dedicated removable card, and
only then rerun with `sudo`, `--write`, `--confirm-device diskN`, and
`--evidence hardware/as-built/phone-001/evidence/recovery-media-phone001-6f50d12.json`.
The writer verifies the signed manifest, writes the expansion, flushes it, and
hashes a complete image-length readback. Eject the card after it passes.

## 2. Assemble once and capture the baseline

Connect the complete phone, insert the verified card, attach the measurement
instrument, and connect Ethernet only if desired; Wi-Fi must also be tested.
Before fault injection, capture serial numbers, PCB revision, installed
artifact identities, wiring deviations, and photographs using
`hardware/as-built/README.md` and `tools/as_built_record.py`. Record cold-boot,
ringer/audio peak, coin-validator, and controlled-brownout measurements while
the complete installed load is present.

The first physical boot must prove that the actual Zero 2 W reaches a stable
slot, the daemon, Alpha, Beta, audio, SIP, controls, update origin, and reverse
tunnel are healthy, and the candidate commits only after the full 125-second
maintenance-tunnel gate. Retain `journalctl`, boot-slot, health, HIL, and MCU
identity output before changing anything else.

## 3. Run Wi-Fi onboarding while local recovery is available

Follow `host/docs/WIFI_ONBOARDING.md`. Exercise wrong credentials, hidden,
open, WPA2, WPA2/WPA3-transition, save-time power loss, radio failure, and the
protected recovery gesture. For iOS, Android, macOS, and Windows, let the native
captive detector open the portal and download that platform's
`acceptance.json`; also record the outcome after AP shutdown, station
reconnection, and maintenance recovery. Add both files with
`tools/handoff_acceptance.py record-wifi`.

Confirm from an untrusted setup client that SSH, the admin API, LAN forwarding,
and stored credentials remain unreachable. Confirm that ordinary Internet,
DNS, update-origin, or tunnel loss does not reopen the AP.

## 4. Run the interruption matrix without rewiring

Use the installed `millennium-physical-interruption` command as described in
`hardware/as-built/README.md`. Capture the before/after state for:

1. idle power loss;
2. active-call power loss;
3. content-save power loss;
4. OTA download interruption;
5. Alpha and Beta MCU-flash interruption;
6. host activation interruption;
7. failed candidate boot and automatic return to the prior slot;
8. a healthy update in each A/B direction.

Remove actual power at every required journal boundary; a reboot command is
not equivalent. For brownout, record the supply, complete load, minimum
voltage, duration, MCU reset causes, filesystem/slot result, and recovery. Add
each resulting evidence file with `tools/as_built_record.py record-test`.

Do not reimage the production card until the dedicated recovery card has passed
write/readback and physical boot. Reimage only from locally attached verified
media, restore only the allowlisted per-device state, and rerun both A/B
directions and recovery afterward.

## 5. Prove genuinely external maintenance

Move the maintenance laptop to the cellular hotspot or another connection that
does not share the home public egress. Run the baseline and audit procedure in
`tools/external_maintenance_audit.py`, use the approved hardware-backed SSH
identity for an interactive session through the known domain, inspect phone
health, and retain the dated privacy-preserving evidence. Add it with
`tools/handoff_acceptance.py record-external`.

## 6. Playtest before changing the setup

With the same assembled phone, run `content/PLAYTEST.md` with at least two
first-time callers. Give only the prescribed neutral invitation. Record both
uncoached primary runs and every repeat, invalid-input, timeout, interruption,
return-visit, offline, and optional-input scenario with
`content/playtest_record.py`. If a participant cannot complete the experience,
record a rejection and the defect; do not coach them and mark it passed.

## 7. Close the handoff gate

Run all three validators:

```bash
python3 tools/as_built_record.py validate hardware/as-built/phone-001/evidence.json
python3 content/playtest_record.py validate hardware/as-built/phone-001/playtest-last-line-2.1.0.json
python3 tools/handoff_acceptance.py validate hardware/as-built/phone-001/handoff.json
```

Only after they pass should the remaining physical and human boxes in
`TODO.md` be checked and the recovery candidate lose its `unapproved` status.
