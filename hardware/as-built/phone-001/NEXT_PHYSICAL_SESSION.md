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
- the Mac with this repository and `UEBuild` attached;
- iOS, Android, macOS, and Windows clients;
- a cellular hotspot or other network that is genuinely outside the home ISP;
- two first-time callers who have not read the story documentation.

Do **not** use the 512 GB `UEBuild` volume as recovery media, regardless of the
`/dev/diskN` number macOS assigns it. It holds the builder, retained images, and
signing-key backup. Identify the recovery card by its reported media name,
capacity, and removable status immediately before writing it.

## 1. Write and verify the corrected recovery card

**Completed 2026-09-06.** macOS identified the dedicated 63.9 GB removable
card as `/dev/disk12`. The signed `2ad5179` image was written, flushed, and
read back over its complete 15,636,365,312-byte image length. The readback
matched the signed expanded SHA-256
`2cd7be17c6cb4711e880c81e630beb50a08b4122cf043fea703a1357eedaf08a`.
The card was then ejected and is safe to remove. Evidence is retained in
`evidence/recovery-media-phone001-2ad5179.json`.

The earlier `/dev/disk10` write/readback passed byte-for-byte, but its v3 image
failed physical boot acceptance and is quarantined. It must not be redeployed.
The corrected `2ad5179` media write is complete; only its physical boot approval
remains open.

The corrected factory-seeded artifact is retained on `anima` at:

```text
/home/kmatzen/.local/share/millennium-recovery/images/zero2w-ab-1.0.0-2ad5179-phone001-unapproved
```

Its signed expanded SHA-256 is
`2cd7be17c6cb4711e880c81e630beb50a08b4122cf043fea703a1357eedaf08a`.
That restricted directory was copied directly to the attached `UEBuild` volume
and its compressed, manifest, and signature hashes plus Ed25519 signature were
verified locally. The seeded image was not placed on the laptop's constrained
internal disk.

The software-verified candidate is intentionally named `unapproved` until the
media write/readback and physical boot both pass:

```text
/Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-2ad5179-phone001-unapproved
```

Do not rewrite the verified card merely to repeat this completed step. If the
card must be replaced or its custody/readback evidence becomes invalid, attach
only the dedicated replacement, identify its current whole-disk name with
`diskutil list external physical`, and run the preflight first:

```bash
python3 tools/write_recovery_media.py \
  --manifest /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-2ad5179-phone001-unapproved/recovery-manifest.json \
  --signature /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-2ad5179-phone001-unapproved/recovery-manifest.json.sig \
  --public-key host/ota/keys/release-2026-08.pem \
  --image /Volumes/UEBuild/millennium-images/zero2w-ab-1.0.0-2ad5179-phone001-unapproved/millennium-zero2w-ab-phone-001.img.zst \
  --target /dev/diskN
```

Read the reported identity, confirm it is the dedicated removable card, and
only then rerun with `sudo`, `--write`, `--confirm-device diskN`, and
`--evidence hardware/as-built/phone-001/evidence/recovery-media-phone001-2ad5179.json`.
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
