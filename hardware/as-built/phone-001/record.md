# As-built record — phone-001

Status: **INCOMPLETE — do not accept for handoff**
Record opened: 2026-08-31 by automated maintenance inventory

The machine-readable remaining-gate audit was refreshed on 2026-09-02 in
`remaining-acceptance-audit-2026-09-02.json`. The complete QEMU software lab
passed at commit `c16c576`, including signed OTA, fault recovery, onboarding,
story traversal, and abrupt-power checkpoint recovery. That result explicitly
claims no physical-hardware coverage. No removable volume was mounted during
the audit, so the two-offline-media key requirement remains open. A prepared
privacy-preserving playtest record for signed content `last-line-2.1.0` is in
`playtest-last-line-2.1.0.json` and intentionally fails validation until two
real first-time callers and every physical resilience scenario are observed.

## Identity

- Device ID: `phone-001` (provisional; owner must confirm asset label)
- Stable LAN address observed: `192.168.86.152`
- Maintenance endpoint: `maintenance.kmatzen.com:2223`, reverse port `22022`
- Hostname observed: `raspberrypi` (stable hostname still required)
- Enclosure/asset serial: **REQUIRED — physical inspection**
- Raspberry Pi serial: `0000000001ba6844`
- Machine-ID SHA-256: `38d4ed85c0bf94de95b54f59285b25ef647b6a882600f419e00aac9d527a0612`
- Keypad MCU identity: USB product `Millennium Alpha`; no unique USB serial
- Display MCU identity: USB product `Millennium Beta`; no unique USB serial
- Owner/location: **REQUIRED — private inventory reference**

## Installed provenance

Production bootstrap sequence 2 committed on 2026-08-31 after direct identity
attestation of both MCUs, protocol-v2 negotiation, SIP registration, daemon
health, and an independent HIL smoke test. The bootstrap was an explicitly
approved, local maintenance installation; it was not a signed release fetched
through the scheduled production timer.

- Release: sequence 2, version `0.4.0`, `armv7l`
- Host: `millennium-daemon 0.4.0 (git unknown, built 2026-08-31T15:34:55Z)`
- Host SHA-256: `f3cb0766e5834260d4bc80432f8c2ccd7329be1a5edbcabae8424d7483c58039`
- OTA worker SHA-256: `a0e3b580938c0350ec277c606628870af8f77278e02ac02e30372018f98fd515`
- Keypad HEX SHA-256: `f1692bb35d07b759428723cd3ec4257eaa3520f6eb91f9aeec5cd3bc472de273`
- Display HEX SHA-256: `5dab000d8408ed497432fd52248c8671156ef764d4f7426e56df3d31e9b4b0a2`
- Keypad identity: role `keypad`, version `0.4.0`, protocol 2, build `e0fe59960549`
- Display identity: role `display`, version `0.4.0`, protocol 2, build `e0fe59960549`
- Content release: `last-line-1.1.0`
- HIL result: passed all eight gates at `2026-08-31T16:07:19Z`

Signed sequence 3 was subsequently published at `updates.kmatzen.com` with
key ID `release-2026-08`, accepted by the normal update-check service, and
committed by the timer-owned automatic-apply service at 2026-08-31 09:57 PDT.
The post-update HIL run passed all eight gates at `2026-08-31T16:57:53Z`.

- New signing public-key SHA-256: `581a1ff72d1867b3dfb3b1ffc5521308ac646878050f1c4c12739e0dedaf7348`
- Installed release: sequence 3, version `0.4.0`, key ID `release-2026-08`
- Encrypted-key recovery evidence: `signing-key-recovery-2026-08-31.json`
- Current-custody audit: `signing-key-backup-audit-2026-09-01.json` found the
  older recorded copies absent, then recorded checksum-matched replacement
  ciphertext on local restricted storage and anima plus a successful in-memory
  sign/verify recovery drill. Two separate removable/offline media are still
  required by the lifecycle procedure before backup acceptance can close.
- First removable copy: on 2026-09-05 macOS identified a 64 GB SD card as
  external/removable with volume UUID `EEE0E3AE-2A6F-37D3-8498-B7AE4AB4D163`.
  The non-overwriting copier verified ciphertext SHA-256
  `0d4b59b1b3ea3d9e9a6563ce033e8e3c6f8fa926a86c65ce1976c8c906f3b8ff`.
  Recovery occurred only on a temporary RAM disk using the Keychain-held
  secret; the public identity matched and a disposable signature verified.
  The RAM disk was destroyed and the card ejected. Evidence is in
  `evidence/key-copy-sd-eee0e3ae-2026-09-05.json` and
  `evidence/key-recovery-sd-eee0e3ae-2026-09-05.json`. One more physically
  distinct removable device remains required.
- Device backup: Restic snapshot `0226382e` on anima, restore-stream checked
  on 2026-08-31; nightly timer enabled with 14 daily, 8 weekly, and 12 monthly
  retention points.

Signed sequence 5 committed through the normal production worker on 2026-08-31
after sequence 4 upgraded the worker's accepted payload format. The release was
scoped to the unique `phone-001` device group. Independent post-commit checks
confirmed the exact host build, both MCU/protocol gates, SIP registration,
maintenance tunnel, serial stability, and all eight HIL gates.

- Installed release: sequence 5, version `0.4.0`, `armv7l`
- Host: `millennium-daemon 0.4.0 (git d227cd0, built 2026-08-31T20:21:55Z)`
- Host SHA-256: `b420a3cc100ae96b1d0e9cc31c38526a1bacc167226b562304d9485dcc3de724`
- Content installer SHA-256: `f5a4b6ce0bdf2a04ec86378f762d99f1ce924cd1e8e00437cbea5910196326d3`
- Story compiler SHA-256: `e2da3cef20ab1fe1cc4ac34f9b3c66ef660286c9409dc6c2f0229d705cc80c45`
- MCU identities: keypad/display version `0.4.0`, protocol 2, build `e0fe59960549`
- HIL result: all eight gates passed at `2026-08-31T21:06:30Z`
- Content release: `last-line-2.0.1`, manifest SHA-256
  `a4d95ec05a9bdf6636b0ca3f202d01f73b17819bf548c4f6cab19b88d3fe944a`
- Live experience check: Story Mode loaded the signed runtime, rendered its
  invitation/missed-call display, and opened its 8 kHz PCM narration inside the
  hardened daemon sandbox; Classic Phone was restored afterward.
- Audio profile: stable ALSA card ID `Device`; systemd permits `char-alsa`.
- Latest encrypted off-device backup observed during this work: Restic snapshot
  `2fbf5133`; phone acknowledgement and monitoring freshness verified.
- Newer encrypted off-device backup: Restic snapshot `15515b87`, created
  `2026-09-01T03:31:44Z`; the anima pull service reports success and remains
  scheduled nightly.

Signed sequence 6 was manually started with explicit owner authorization after
the configured `02:00`–`05:00` automatic window deferred its daytime timer run.
It committed at 2026-08-31 16:03 PDT with daemon commit `16d759e`, the corrected
content installer, unchanged attested MCU images, healthy SIP/serial status,
and all eight HIL gates passing at `2026-08-31T23:03:25Z`.

A deliberately unhealthy, signed, `phone-001`-only sequence 7 was then applied
to prove the production rollback path. Its daemon exposed the correct package
version to the release builder but deliberately exited on normal startup. The
OTA worker waited the full 150-second health window, rejected the release at
2026-08-31 16:07 PDT, restored `current` to sequence 6, restarted the known-good
daemon, cleared the activation journal, and left `installed-sequence` at 6.
No manual rollback action was taken, and unchanged firmware digests avoided MCU
reflashing. Signed recovery sequence 8 subsequently committed at 16:10 PDT.

- Active release: sequence 8, version `0.4.0`, `armv7l`
- Rollback release: sequence 6, version `0.4.0`, `armv7l`
- Host: `millennium-daemon 0.4.0 (git 16d759e, built 2026-08-31T21:36:17Z)`
- Host SHA-256: `72f56cc610f2fc56afebf0f5d8724b753e9a68ae8ee7d86eaa8343562659358e`
- Keypad HEX SHA-256: `f1692bb35d07b759428723cd3ec4257eaa3520f6eb91f9aeec5cd3bc472de273`
- Display HEX SHA-256: `5dab000d8408ed497432fd52248c8671156ef764d4f7426e56df3d31e9b4b0a2`
- Recovery HIL: all eight gates passed at `2026-08-31T23:10:56Z`

### Superseded sequence 1 provenance

- Release: sequence 1, version `0.4.0`, `armv7l`
- Host: `millennium-daemon 0.4.0 (git 9960d29-dirty, built 2026-08-31T04:32:56Z)`
- Host SHA-256: `7b8f90288311afdbb6b4576600f5a2efd9453666b4e03ead1faa47d758380253`
- Keypad HEX SHA-256: `0a4615ec9a2cb9d79f528e362f32686084f63dfc5970782ccbf6b01a297bb64d`
- Display HEX SHA-256: `463948677ebba266bee744a31e7edaa9867510a2ce240d681a49d692c1528e79`
- OTA public-key DER SHA-256: `46d4ea8e2e8cf261816953b5c64f907840a5da84f50c3127011d4bbe1d77c946`
- Content release: absent before rollout
- Reported MCU identities: unavailable in legacy firmware; protocol-v2
  attestation is required after the approved coordinated rollout.

## Fabrication provenance awaiting physical revision confirmation

The repository's current `phonev6` fabrication set has these hashes, but it
must not be claimed as installed until the board marking or assembly record is
checked:

- Schematic: `2480518e75281b795a437cdba24fe009741dfdf52aa6fc7f2b1179401b806601`
- PCB layout: `b4325fb27b8b2bf12a40f7dc6bf17a41936c0d802424ff9b3ba59439d76b733e`
- BOM: `b67ab8f81a35e23550cb3d6d4e703cf0bbbb6f6aaef89800a26ff9a871e761a9`
- Gerber archive: `29a7e7eb6e1090f657f7638b71fb50d656755ee47833b3bf5203e4e561c17fd0`
- Installed PCB revision: **REQUIRED — physical inspection**

## Physical construction

- Wiring deviations/bodge wires: **REQUIRED — physical inspection**
- Audio interface: USB `CARD=Device`; playback profile `out_right_solo`
- Power supplies and ratings: **REQUIRED — physical inspection**
- Assembly photographs and hashes: **REQUIRED**

## Electrical and recovery validation

No row below may be inferred from software tests.

| Test | Instrument/load | Minimum voltage | Result | Evidence/date |
| --- | --- | ---: | --- | --- |
| Cold boot | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Ringer/audio peak | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Coin validator operation | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Controlled brownout | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Arbitrary idle power loss | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| OTA download interruption | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| MCU flash interruption/recovery | REQUIRED | REQUIRED | REQUIRED | REQUIRED |
| Host activation interruption/rollback | Software activation journal | N/A | PASS (three automatic restorations during rollout debugging) | 2026-08-31 system journal |

## Remote-maintenance evidence

On 2026-08-31, a YubiKey-authenticated session reached this host through
`maintenance.kmatzen.com` and `anima`'s loopback reverse port, without using the
phone's inbound LAN SSH address. A separate test from a genuinely external
network remains required for release acceptance.

The live audit in `maintenance-access-audit-2026-09-01.json` found that the
restricted metrics tunnel remains healthy, but interactive administration now
initially rejected the local hardware-backed identities. A later retry with the
explicit anima no-touch FIDO identity succeeded through the reverse port; no
phone configuration or key repair was necessary. Exact live release, MCU path,
content, and daemon evidence is recorded in `live-inventory-2026-09-01.json`.
The required external-network vantage-point test is still outstanding.

A fresh read-only audit on 2026-09-02 restored the local OpenSSH FIDO helper,
proved the hardware key with a disposable local signature, and reached the
phone through anima's loopback reverse port `22022`. The phone remained on
signed sequence 8 with healthy serial and SIP checks, zero daemon restarts,
and active maintenance/update services. Signed sequence 9 was verified as
pending for `phone-001` and was correctly deferred outside the automatic
installation window. Exact evidence is in `live-inventory-2026-09-02.json` and
`maintenance-access-audit-2026-09-02.json`. Because the administrative client
was still physically on the home network, this does not close the genuinely
external-vantage requirement.

Signed sequence 9 was explicitly applied outside the maintenance window on
2026-09-01 and committed at 21:41 PDT. The active release is now sequence 9,
with sequence 8 retained as its rollback release. The deployed host SHA-256 is
`e07eaab73dfa273b93df46b17ec8b1dd9ef1e3fb2d0fa6ee556e0b7be96276c8`;
both MCU images remain the attested protocol-v2 builds above. Independent HIL
checks passed all eight gates at `2026-09-02T04:42:22Z` and again after content
activation at `2026-09-02T04:47:13Z`.

The replacement signed experience `last-line-2.1.0` was then installed through
the sequence-9-owned content verifier. Its signature, archive digest, author
JSON, and compiled runtime were verified before activation. A live atomic
rollback selected `last-line-2.0.1`, and a second rollback restored 2.1.0;
the final runtime SHA-256 is
`0ad71cb2590684f3b730fc9c0129e20bfc38a1091a908a45c01efded67af6b27`.
The daemon remained healthy with serial, SIP, and both MCU links healthy.
A fresh restricted encrypted backup pull completed on anima immediately after
deployment as Restic snapshot `40316409` at `2026-09-02T04:51:06Z`; the normal
retention policy completed successfully.

A privacy-preserving home-network baseline was captured through anima at
`2026-09-02T05:11:08Z`. It stores only an HMAC of the source address; its random
secret remains in restricted local configuration. A live audit from the same
network was rejected before producing acceptance evidence. The outstanding
external test can use the public maintenance hostname while binding it to the
same stable anima server identity.

The durable physical-interruption harness and boot reconciler were installed
on phone-001 on 2026-09-01. `systemd-analyze verify` accepted the unit, the
reconcile service is enabled, and the harness reported `idle`; no destructive
test was armed. It is ready to capture before/after evidence when a physical
operator performs the remaining power and network interruptions.

On 2026-09-05 the OTA-download network case was completed using a physically
isolated GL.iNet Ethernet segment while the Wi-Fi reverse tunnel provided an
independent observation path. The phone verified the unchanged production
sequence-9 manifest before the operator removed RJ45. A Pi-facing throttled
relay recorded a broken pipe after 170045 response bytes, the phone downloader
recorded a read timeout, the direct address became unreachable, and the
maintenance tunnel remained active. After RJ45 restoration, the direct address
returned without reboot, the complete 1267485-byte bundle matched SHA-256
`262dbf316ae484a92c893fcde384f1444a35c1e6695f835829816d224519989e`,
and all eight HIL gates passed with sequence 9, release 0.4.0, and content
2.1.0 unchanged. The composite record is
`evidence/ota-download-interruption-2026-09-05.json`. The controller board was
disconnected from the phone peripherals during this network-only test, so the
record makes no peripheral or measured-load claim.

The anima backup workflow was extended and exercised on 2026-09-01. Phone
snapshot `044b0426` completed together with server-state snapshot `f5472258`.
The latter includes the update origin, doormand configuration, maintenance
units and scripts, authorized public keys, and recovery instructions. It was
streamed back through Restic and its allowlisted contents were verified without
staging a plaintext archive. Exact evidence is in
`server-state-backup-2026-09-02.json`.

The two removable copies made on 2026-09-05 were later found to contain the
legacy `primary` key, not the active `release-2026-08` key. Their recovery
drills were mechanically successful, but the validation compared them with a
mislabeled local public key instead of the key selected by the production
manifest. The records now identify those copies as `legacy-primary`, and they
have been removed from `handoff.json`; they do not satisfy release-key custody.

The original active-key AES ciphertext was recovered from anima with its
original SHA-256 `a94302f82874284020aeca7b3ccc7543fb014e4207d4268e17aaa1b8aea9ccad`.
It was decrypted only on a disposable RAM disk using the Keychain secret and
matched the production canonical public-key identity
`581a1ff72d1867b3dfb3b1ffc5521308ac646878050f1c4c12739e0dedaf7348`.
The local restricted vault was repaired without deleting the legacy backup.
The generic local public-key path now contains the active key, while the
legacy public and private files were preserved under explicit
`legacy-primary-*` names; no plaintext active private key is stored there.
Two new removable copies of this active ciphertext were required. Exact
findings are in `evidence/key-custody-correction-2026-09-06.json`.

The first corrected active-key removable copy was created on `SD-EEE0E3AE` on
2026-09-06 without overwriting its preserved legacy copy or Raspberry Pi boot
files. Ciphertext SHA-256 `a94302f82874284020aeca7b3ccc7543fb014e4207d4268e17aaa1b8aea9ccad`
was verified on the card, recovered only into an APFS RAM disk, matched the
active canonical public identity `581a1ff72d1867b3dfb3b1ffc5521308ac646878050f1c4c12739e0dedaf7348`,
and produced a verified disposable Ed25519 signature. The RAM disk was
destroyed and the SD card safely ejected. Exact evidence is in
`evidence/key-copy-active-sd-eee0e3ae-2026-09-06.json` and
`evidence/key-recovery-active-sd-eee0e3ae-2026-09-06.json`.

The OTA format was hardened so every new signed bundle contains the full
source commit and rejects unknown, dirty, missing, or daemon-mismatched source
identities. A first sequence-10 publication was rejected by the phone because
it exposed the key-label error above; it never activated, and the stable
pointer was immediately restored to sequence 9. The immutable, unreferenced
sequence-10 directory was retained as audit evidence rather than overwritten.

Sequence 11 was rebuilt from commit
`906c58682097e128473c29db49ce1f4ed5b03a6a`, signed with the recovered active
key, accepted by the phone, and committed through the rollback-protected OTA
path on 2026-09-06 UTC. Sequence 9 remains the rollback release. The active
daemon SHA-256 is
`434c7706de1fa581011c2eccd7794f8a3d244b80c0cb9686322aca9cbd9e7fdd`;
the unchanged MCU images retain their previously attested hashes. A post-rollout
HIL run passed all eight gates at `2026-09-06T00:05:35Z`. Exact evidence is in
`evidence/sequence-11-source-attested-rollout-2026-09-06.json`.
