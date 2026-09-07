# Phone 001 final physical acceptance session

This is the shortest safe path through every remaining hardware-dependent
gate. Keep the phone assembled and connected for the entire session; the order
below is intended to avoid another teardown or network rewire. None of these
steps may be replaced by QEMU evidence.

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

**Pending for `7f94bdd`.** The earlier media write/readback completed, but its
physical boot rejected the image. Its evidence remains historical only and
does not satisfy this gate for the current candidate.

The earlier `/dev/disk10` write/readback passed byte-for-byte, but its v3 image
failed physical boot acceptance and is quarantined. It must not be redeployed.
The corrected `2ad5179` media write completed, but its 2026-09-07 physical boot
exposed a missing `wpasupplicant` package and missing monitor output directory.
It is now rejected evidence and must not be redeployed. Build, sign, write, and
fully read back a newer candidate containing both source fixes before resuming
physical boot approval.

The superseded `2ad5179` artifact remains rejected. The newer factory-seeded
and signed artifact containing the Wi-Fi package, monitor tmpfiles, daemon
token-permission, and active trust-root corrections is retained on `anima` at:

```text
/home/kmatzen/.local/share/millennium-recovery/images/zero2w-ab-1.0.0-7f94bdd-phone001-unapproved
```

Its signed compressed SHA-256 is
`994eb3d83bc898f267017497f9b5c14dabb0358a56117ec198126f0e0b775e60` and
its signed expanded SHA-256 is
`b37ab43d14da59b887f336f10252f4e7b0607c8ec04532b66a7797901a5970a5`.
The Ed25519 signature and Zstandard stream were independently verified on
`anima`; the signing operation and active public-key identity were also
verified on macOS. A bounded copy is staged on the external `UEBuild` volume;
its four hashes, signature, and Zstandard stream were reverified without
placing the image on the laptop's internal disk. It has not yet been written
to recovery media.

The software-verified candidate is intentionally named `unapproved` until the
media write/readback and physical boot both pass:

```text
/Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-7f94bdd-phone001-unapproved
```

Shut down the currently booted rejected image, remove its dedicated recovery
card, and attach that card to the Mac. Identify its current whole-disk name
with `diskutil list external physical`. Transfer the restricted artifact
directly from `anima` to `UEBuild` for the bounded write session; do not place
the image on the laptop's internal disk. Verify the four recorded hashes and
signature after transfer, then run the preflight first:

```bash
python3 tools/write_recovery_media.py \
  --manifest /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-7f94bdd-phone001-unapproved/recovery-manifest.json \
  --signature /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-7f94bdd-phone001-unapproved/recovery-manifest.json.sig \
  --public-key /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-7f94bdd-phone001-unapproved/update-signing-key.pem \
  --image /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-7f94bdd-phone001-unapproved/millennium-zero2w-ab-phone-001.img.zst \
  --target /dev/diskN
```

Read the reported identity, confirm it is the dedicated removable card, and
only then rerun with `sudo`, `--write`, `--confirm-device diskN`, and
`--evidence hardware/as-built/phone-001/evidence/recovery-media-phone001-7f94bdd.json`.
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
tunnel are healthy, and the candidate commits only after the full 95-second
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
