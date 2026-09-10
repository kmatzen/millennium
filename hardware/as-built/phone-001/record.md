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

The second corrected copy was created on the physically distinct 512.1 GB
`NVME-68D6C050` volume UUID `68D6C050-587F-455B-B0B0-77DC06AC40E8` on
2026-09-06. Its ciphertext matched the first copy and restricted vault, its
RAM-disk recovery matched the same active canonical public identity, and its
fresh disposable Ed25519 signature verified. The RAM disk was destroyed and
the NVMe was safely ejected. Exact evidence is in
`evidence/key-copy-active-nvme-68d6c050-2026-09-06.json` and
`evidence/key-recovery-active-nvme-68d6c050-2026-09-06.json`.

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

On 2026-09-06 a fresh Zero 2 W whole-disk A/B recovery candidate was built from
source commit `25c27294725467dd679711a8d8e2ce8dd49af5e0`. Its canonical manifest
binds the 444616057-byte Zstandard image and 15636365312-byte expansion, exact
MBR partition layout, board model, architecture, and key ID. The manifest was
signed with `release-2026-08` using a private key recovered only on a volatile
RAM disk; independent verification matched canonical public-key identity
`581a1ff72d1867b3dfb3b1ffc5521308ac646878050f1c4c12739e0dedaf7348`, and
the plaintext workspace was destroyed. The artifact remains explicitly named
`unapproved`: it has not yet been written to dedicated recovery media, read
back, or booted on a physical Zero 2 W. Exact hashes and scope are in
`evidence/zero2w-recovery-artifact-25c2729-2026-09-06.json`.

The signed candidate also has an independently verified restricted copy on
`anima`. All transferred hashes, the Ed25519 signature, and the canonical
public-key identity were checked before atomic publication. Encrypted Restic
snapshot `e51ac235` then captured the server recovery store without staging a
plaintext archive; a fresh restore stream reproduced the exact whole-image
SHA-256. Exact evidence is in
`evidence/zero2w-recovery-retention-25c2729-2026-09-06.json`.

The `25c2729` candidate was subsequently superseded after auditing its
first-boot health gate. Source commit
`5a9eef204a1fb5d71f5c8a1888e35a963d01164f` requires an actual bounded ALSA
PCM write, fresh zero-drop diagnostics from both Alpha and Beta, named serial
and SIP health, a valid local-control state, and 95 seconds of continuous
reverse-tunnel service activity—past SSH's complete server-alive failure
window. A fresh whole-disk image built from that commit passed the raw MBR,
boot-content, minimal-rootfs, embedded-source, and compressed-stream checks and
was signed with the same RAM-only `release-2026-08` procedure. This replacement
also remains explicitly unapproved pending a dedicated-media readback and real
Zero 2 W boot. Exact evidence is in
`evidence/zero2w-recovery-artifact-5a9eef2-2026-09-06.json`.

The replacement candidate was then copied into a new, non-overwriting,
restricted directory on `anima`. The destination independently verified every
transferred hash, the canonical Ed25519 public-key identity and signature, and
the Zstandard stream before atomic publication. Encrypted Restic snapshot
`869fd021` captured the recovery store, and a fresh restore stream reproduced
the exact image SHA-256 without staging a plaintext archive. Exact evidence is
in `evidence/zero2w-recovery-retention-5a9eef2-2026-09-06.json`.

The `5a9eef2` candidate was subsequently superseded when the pinned firmware
reproducibility job proved that its checked-in Alpha and Beta HEX files still
carried the older build identity `e0fe59960549`. Both firmwares were rebuilt in
the pinned toolchain and refreshed with build identity `0f9093be5263`. A clean
native ARM64 image was then built from commit
`31eda512ab94cf7d9959451673e2674d44516270`. Its embedded keypad and display
firmwares exactly match the refreshed source artifacts with SHA-256 values
`928e4c8da74746ae4107771443d0467b893101308ee224e0bbc712016dc82564` and
`511518a53c5ad37bb75b774c2f00b04c713877f136a7699ff15211ca92018440`.
The full MBR A/B, raw boot-content, minimal-rootfs, embedded-source, health-gate,
firmware-identity, and compressed-stream checks passed. The canonical manifest
binds compressed image SHA-256
`baa2a108ebb6af2e13646f4a4a043ba6b8ebdffbfec2bc4579bd97279d798a66`
to expanded SHA-256
`7eca956c1d94bc0cd706a9152aae78262bafb2163d113959448df076469ba128`.
It was signed with the active `release-2026-08` key recovered only on a
disposable RAM disk; the public identity and signature were independently
verified and the RAM disk was ejected. This candidate remains explicitly
`unapproved` pending a physical Zero 2 W boot.
Exact evidence is in
`evidence/zero2w-recovery-artifact-31eda51-2026-09-06.json`.

The `31eda51` candidate was copied into a new, non-overwriting restricted
directory on `anima`. The destination independently verified the transferred
hashes, active Ed25519 public-key identity, manifest signature, and Zstandard
stream before atomic publication. Encrypted Restic snapshot `050a82a0`
captured the server recovery store without staging a plaintext archive. A
fresh restore stream from that snapshot reproduced compressed image SHA-256
`baa2a108ebb6af2e13646f4a4a043ba6b8ebdffbfec2bc4579bd97279d798a66`.
Exact evidence is in
`evidence/zero2w-recovery-retention-31eda51-2026-09-06.json`.

On 2026-09-06 macOS identified a dedicated 63.9 GB USB SD card as removable
whole disk `/dev/disk10`. The recovery-media writer verified the canonical
manifest signature and compressed artifact, held exclusive raw-device access
through the write and mandatory full image-length readback, and reproduced the
signed expanded SHA-256
`7eca956c1d94bc0cd706a9152aae78262bafb2163d113959448df076469ba128`.
The card was safely ejected immediately afterward. This closes media creation
and byte-for-byte readback only; the candidate remains `unapproved` until it
boots and passes its health gates on the physical production-equivalent Zero
2 W. Exact evidence is in `evidence/recovery-media-31eda51.json`.

The first physical boot rejected that generic candidate operationally. It
appeared at `192.168.8.239` and served SSH, but neither the approved maintainer
key nor the intended appliance health endpoint was available. Offline ext4
inspection proved the image had no `factory-provision.json`, an empty
`authorized_keys`, and a residual `root@millennium-qemu` public host key. It
was therefore never a deployable phone-001 recovery image despite its valid
software signature and media digest. Exact rejection evidence is in
`evidence/recovery-generic-boot-rejection-31eda51-2026-09-06.json`.

A corrected candidate was rebuilt on `anima` from the same verified software
image and the newest pre-reimage phone backup. Factory provisioning installed
fresh phone-001 machine and SSH host identities, the approved hardware-backed
maintainer key, the active update trust root, maintenance tunnel state, and a
new per-device Wi-Fi onboarding secret. Both system slots and the persistent
partition were independently remounted read-only in a network-disabled
container and their identities matched the factory record. The canonical
manifest binds compressed SHA-256
`780b17b745644b691eb723fe9fb00f66972507ddf8ec48e8b091547abe54688f`
to expanded SHA-256
`b2d605cc2b5bd008dfa50530abdc5b52f3e081234f95bf54c633f720bdf4f673`.
It was signed on a temporary APFS RAM disk with `release-2026-08`, verified on
both the Mac and `anima`, and the RAM disk was ejected. Anima atomically retains
the candidate and separately protects the owner Wi-Fi handoff record. A second
exclusive write and complete 15,636,365,312-byte readback on `/dev/disk10`
reproduced the signed expanded digest, after which the card was ejected. The
candidate remains `unapproved` pending its corrected physical Zero 2 W boot.
Exact build evidence is in
`evidence/zero2w-recovery-seeded-artifact-31eda51-2026-09-06.json`; media
evidence is in `evidence/recovery-media-phone001-31eda51.json`.

The `2ad5179` physical boot subsequently exposed the missing `wpasupplicant`
package and monitor collector creation defect. Source `7f94bdd03c5623eb2588776bc8d909c294ca84fa`
fixes both defects and the factory-token runtime permission mismatch. A clean
ARM64 image build on `anima` passed the production-rootfs, A/B MBR, and raw
boot-content contracts. Factory seeding preserved phone-001 identity while a
pre-signing audit caught and quarantined an inherited legacy update trust root.
A fresh seed installed the active `release-2026-08` public key in both system
slots and persistent storage. The corrected artifact is retained only on
`anima` at `zero2w-ab-1.0.0-7f94bdd-phone001-unapproved`, with compressed
SHA-256 `994eb3d83bc898f267017497f9b5c14dabb0358a56117ec198126f0e0b775e60`
and expanded SHA-256
`b37ab43d14da59b887f336f10252f4e7b0607c8ec04532b66a7797901a5970a5`.
It was signed on a disposable APFS RAM disk and verified on macOS and `anima`.
A bounded copy on external `UEBuild` passed all four hashes, signature, and
Zstandard integrity checks without using laptop internal storage. It remains
unapproved pending physical boot. On 2026-09-07 macOS identified the dedicated
63.9 GB card as `/dev/disk6`; signed preflight, full write, flush, and complete
15,636,365,312-byte readback passed. The readback matched expanded SHA-256
`b37ab43d14da59b887f336f10252f4e7b0607c8ec04532b66a7797901a5970a5`,
and the card was ejected. Exact media evidence is in
`evidence/recovery-media-phone001-7f94bdd.json`.
Exact evidence is in
`evidence/zero2w-recovery-seeded-artifact-7f94bdd-2026-09-07.json`.

That image's physical boot exposed a host protocol bug rather than an MCU
hardware failure: startup commands overwrote the single pending ACK slot, and
a valid MCU busy response aged into a false serial failure. Commit
`5d7bdda57565ab141a2847b6169f2254ad5e2428` serializes critical commands and
treats matching busy replies as backpressure. A production-linked diagnostic
daemon ran on phone-001 for five minutes with zero reconnects, zero command
timeouts, and no increase from the existing 229 DWC OTG warnings. The failed
boot and diagnostic limits are retained in
`evidence/physical-boot-rejection-7f94bdd-2026-09-07.json`.

A replacement image was then built entirely on `anima`, factory-seeded with
the same phone-001 identity, and independently verified across both read-only
system slots and persistent storage. Its retained signed artifact is
`zero2w-ab-1.0.0-5d7bdda-phone001-unapproved`, with compressed SHA-256
`9af00cad8f947264b0a4d55be479cd3a6aa43883def56e963cd615a6a74419b9`
and expanded SHA-256
`372f3c726285d485954f55ed7b681e215515f411691cbc9f891e45b70d3901dd`.
The release private key existed only on a disposable APFS RAM disk, the
signature verified on macOS and `anima`, and the RAM disk was ejected. The
candidate was copied directly from `anima` to external `UEBuild` without using
laptop internal storage. All four file hashes, the manifest binding to
`5d7bdda57565ab141a2847b6169f2254ad5e2428`, the Ed25519 signature, and the
Zstandard stream verified locally. On 2026-09-07 the dedicated 63.9 GB USB
card appeared as `/dev/disk6`; signed preflight, full write, flush, and complete
15,636,365,312-byte readback passed. The readback matched expanded SHA-256
`372f3c726285d485954f55ed7b681e215515f411691cbc9f891e45b70d3901dd`,
and the card was ejected. It remains unapproved pending physical boot. Exact
build evidence is in
`evidence/zero2w-recovery-seeded-artifact-5d7bdda-2026-09-08.json`; exact media
evidence is in `evidence/recovery-media-phone001-5d7bdda.json`.

The resulting card booted the physical Zero 2 W on 2026-09-07. The exact
production daemon matched SHA-256
`c60dc0f10e732f6c727b11db6bf6e8a23918f58bba215278c5b48d81bfa12f86`,
both Alpha and Beta enumerated, the active root was read-only, and all three
persistent bind mounts were present. The daemon remained active and its serial
and activity health gauges were healthy. Three command timeouts, two serial
reconnects, ten retries, and two frame errors occurred during the first eight
seconds of startup, but every counter remained unchanged between 357 and 427
seconds uptime; no later command timeout or reconnect appeared. The prior RCU
stall, r8152 watchdog, and transmit timeout did not recur. This is a scoped
offline platform/MCU pass only: the setup network had no upstream DNS, and the
handset, audio path, keypad, coin validator, and card reader were not attached.
SIP, the reverse tunnel, complete HIL, installed-load power, and peripheral
acceptance remain open. Exact evidence is in
`evidence/physical-boot-offline-platform-5d7bdda-2026-09-07.json`.

Wi-Fi onboarding on that boot exposed two additional image defects: systemd
removed the bootstrap service's runtime directory when the oneshot exited, and
the staged `millennium-wifi` sysusers declaration had not been executed in the
target root. The setup AP therefore existed, but its helper and portal could
not start. Commit `8ac303b638ee771fe902be6ee0aafdb157a33da0` preserves the
runtime directory, evaluates the setup marker after bootstrap ordering, and
creates and asserts the service account during image construction. A clean
replacement image built on `anima` passed the MBR, raw boot-content, production
minimality, both-slot read-only, persistent-state, trust-root, daemon, and
portal-runtime checks. Its signed compressed SHA-256 is
`8745362a7bbd00fc37d852e9fca993ee92ee894529dd54e8cce2aa9e0991f9f3`;
its expanded SHA-256 is
`ab2937d9ff3848e91bfb4300adb2591a301e08ef6fdeace1dbe81883cd2672e8`.
An explicitly authorized direct copy to external `UEBuild` passed all four
hashes, the Ed25519 signature, and Zstandard integrity without using laptop
internal storage. On 2026-09-08, the repository QEMU harness on `anima` passed
all 17 Wi-Fi onboarding tests, including platform probes, rollback, captive
portal responses, and private credential handoff. The stronger exact-image
test then booted the factory-seeded raw image's own Debian 13 userspace and
verified its production mount graph, D-Bus, resolver, nftables, and Millennium
firewall. The complete 14-layer software lab also passed virtual MCU and
peripheral fault injection, signed host and OS OTA recovery, offline and signed
experience lifecycles, network isolation, and abrupt-power/checkpoint recovery.
QEMU does not emulate the Raspberry Pi firmware, BCM2710A1 radio,
native SD/USB behavior, connected phone peripherals, or installed-load power,
so the candidate remains unapproved pending media write/readback and physical
boot. Exact evidence is in
`evidence/zero2w-recovery-seeded-artifact-8ac303b-2026-09-08.json`.

That candidate's physical boot exposed an upstream image-generator ordering
defect: the upstream shared-state generator overwrote the project generator
after payload staging, leaving generated mount units outside
`local-fs.target.requires`. A new candidate was built from source commit
`2ad5179b0b2de4958a34b845f2db60775e317e07`; its final cleanup hook installs
and byte-compares the project generator after the upstream overlay. The raw
image, boot contents, production minimality, ARM64 payload, Wi-Fi-state mode,
and all six activated shared mounts passed independent checks. Both seeded
system slots and the persistent partition were then remounted read-only and
verified. The signed, factory-seeded candidate is retained on `anima` at
`zero2w-ab-1.0.0-2ad5179-phone001-unapproved`, with compressed SHA-256
`b584b827e9574575c7be65d8729818c465d5d95a7caf22f47e508c6e2e10c397`
and expanded SHA-256
`2cd7be17c6cb4711e880c81e630beb50a08b4122cf043fea703a1357eedaf08a`.
It remained unapproved until its first physical boot on 2026-09-07.
On 2026-09-06, macOS identified the dedicated 63.9 GB removable USB card as
`/dev/disk12`. The recovery writer verified the manifest signature and
compressed image, wrote and flushed all 15,636,365,312 bytes, then read back
the complete image length. The readback reproduced signed expanded SHA-256
`2cd7be17c6cb4711e880c81e630beb50a08b4122cf043fea703a1357eedaf08a`,
and the card was ejected. The evidence file has SHA-256
`4bfc40e007a2a59085b83ee032b5d42aacd8a6932fe1ede2d64a3e6fbba74a28`.
Its `completed_at` value reflects operation start because of a writer timestamp
defect discovered immediately afterward; the file was emitted at
2026-09-06T21:26:05Z, and the writer now records both start and true completion
for future evidence. Exact build evidence is in
`evidence/zero2w-recovery-seeded-artifact-2ad5179-2026-09-06.json`.
Exact media evidence is in
`evidence/recovery-media-phone001-2ad5179.json`.
Encrypted Restic snapshot `7ff8a7d2` includes the retained artifact and passed
an immediate restore-stream listing without staging a plaintext archive.
The same artifact was copied directly from `anima` to `UEBuild`; all three
recorded hashes and the Ed25519 manifest signature verified locally without
using laptop internal storage.

On 2026-09-07 the `2ad5179` recovery card booted the physical Zero 2 W to a
Debian 13 `millennium-phone` login prompt. The factory-bound maintainer key
authenticated over the attached RTL8153 link at `192.168.8.239`; the active
root was `mmcblk0p5`, partition 7 mounted read-write, and NetworkManager, SSH,
the daemon, and maintenance-tunnel units started. Acceptance nevertheless
failed. NetworkManager could not D-Bus activate `wpa_supplicant` because the
minimal image omitted the `wpasupplicant` package, leaving `wlan0` unavailable
and preventing the setup AP from starting. The monitor unit also failed its
mount namespace because `/var/lib/node_exporter/textfile_collector` had not
been created. This candidate remains rejected and must not be approved or
redeployed. Exact evidence is in
`evidence/physical-boot-rejection-2ad5179-2026-09-07.json`. The same read-only
session proved the active system slot was mounted read-only and partition 7
backed writable `/persistent`, `/etc/millennium`, and `/var/lib/millennium`.
The daemon and maintenance tunnel did not reach healthy steady state: neither
Arduino was attached, so the configured Beta serial path was absent, and the
isolated Mac-to-phone Ethernet link did not resolve the public maintenance
hostname. Those are incomplete-test-fixture conditions, not approvals or
additional image regressions.

The backup puller was hardened after a disconnected phone caused Restic to
commit a zero-byte stdin snapshot before shell `pipefail` observed the SSH
failure. Each run now has a unique tag, failed snapshots are removed by exact
ID, saved phone archives are restore-stream checked before acknowledgement,
and server-state backup proceeds independently. A live phone-offline test on
`anima` removed incomplete snapshot `59a73394`, kept the valid phone snapshot
count at eight, and created restore-verified server snapshot `93cb6f42`. Exact
evidence is in `evidence/backup-failed-export-cleanup-2026-09-06.json`.

The same signing ceremony caught a separate custody-label defect before
signing: the `0d4b59...` ciphertext in `/data/backups` derives a public key
that does not match the active `release-2026-08` trust root. The verified
`a94302...` copy was used instead. The mismatched ciphertext was hash-verified
and renamed in place with a `quarantined-key-mismatch` suffix; it must not be
used unless its identity is deliberately reclassified. See
`evidence/signing-vault-mismatch-2026-09-06.json`.

After the observable MCU-health work, source commit
`6f50d127e5813e583e6898a6986175614422470a` was rebuilt from scratch on
`anima`. Both Arduino sketches were compiled with Arduino AVR core 1.8.8 and
Arduino CLI 1.5.1, and the resulting firmware is part of the source commit.
The factory-seeded image passed raw MBR and boot-content validation, production
minimality, read-only verification of both system slots and the persistent
partition, exact daemon/source checks, Wi-Fi runtime checks, and active trust
root verification. The signed compressed SHA-256 is
`4509d94f76d12056dbad1d2c92c7281cd8924c1375814cc05d6b9696d5f55efd`;
the expanded SHA-256 is
`f5c6bd4954374b0f06e206c748f096eb24964f767de0fff8d6036ce4790962b8`.
The retained copy on `anima` and the direct external `UEBuild` copy passed all
four hashes, Ed25519 signature verification, and complete Zstandard expansion
without using laptop internal storage.

The complete QEMU software lab then passed all 17 acceptance layers against
that exact factory image's userspace. Coverage included an independent setup
client traversing the QEMU boundary, portal session/CSRF/private credential
handoff, external HTTPS OTA and injected origin failure, sustained reverse
maintenance tunnel recovery, virtual MCU/peripheral faults, signed host and OS
OTA recovery, offline and signed experience lifecycles, network isolation, and
abrupt-power/checkpoint recovery. The exact-image transport uses a generic
arm64 QEMU kernel and purpose-built initramfs to inject the production A/B
device aliases; a preliminary attempt with the Pi production initramfs was
correctly rejected because it could not find the physical slot aliases under
QEMU. Neither run claims Raspberry Pi firmware, radio, native SD/USB, audio,
installed-load power, or attached phone-peripheral fidelity. Exact evidence is
in `evidence/zero2w-recovery-seeded-artifact-6f50d12-2026-09-08.json`. The
candidate remained explicitly unapproved pending dedicated-media write/readback
and physical acceptance at the time of that software test.

On 2026-09-08 macOS identified the dedicated 63.9 GB USB recovery card as
`/dev/disk6`, distinct from the 512 GB `UEBuild` source. The media writer
reverified the signed artifact, wrote and flushed all 15,636,365,312 expanded
bytes, and hashed the complete image length back. The readback matched signed
SHA-256
`f5c6bd4954374b0f06e206c748f096eb24964f767de0fff8d6036ce4790962b8`.
The card was safely ejected. Exact evidence is in
`evidence/recovery-media-phone001-6f50d12.json`; physical boot acceptance
remains open.

The `6f50d12` card then booted the physical Zero 2 W from system slot A. The
root filesystem was read-only, partition 7 was read-write, the factory
maintainer key authenticated, the daemon and setup portal started, the real
portal template rendered and accepted an owner-network request, and both Alpha
and Beta enumerated with their independent health gauges up and zero reported
I2C drops. The candidate is nevertheless rejected. NetworkManager's
automatically generated USB Ethernet profile installed a lower-metric default
route through the isolated maintenance link, blackholing the otherwise valid
Wi-Fi route. The image also omitted the RTL8153 firmware blob, and persistent
content ownership caused systemd-tmpfiles unsafe-transition warnings. Commit
`38350ac` fixes all three and updates monitor metric names. The Wi-Fi helper's
service deadline then exposed two more faults: resolver latency was not bounded
at the process level, and `Restart=on-failure` reopened setup after the hard
15-minute timeout. Commit `03b5382` uses a bounded curl child, stops setup
services immediately on success, and makes the hard timeout non-restarting.
No owner SSID, passphrase, SIP credential, client address, or user agent is in
the evidence. Exact rejection evidence is in
`evidence/physical-boot-rejection-6f50d12-2026-09-08.json`.

Replacement source `c9b01fb2afb56ddf820ff34fa235ea4dbf2bb5b8` incorporates
those physical findings. Its exact factory-seeded image passed the raw A/B and
boot-content contracts and the complete 17-layer QEMU lab, including an
independent captive-portal client, external OTA failure recovery, reverse
maintenance recovery, virtual MCU/peripheral faults, signed experience
lifecycle, and abrupt-power recovery. Commit `bba5296` strengthened the exact
image gate to assert the assembled USB-Ethernet `never-default` policy and the
bounded, non-restarting Wi-Fi helper. The full result SHA-256 is
`fb6c37274437ba9abd8799a7f779f4adc5394ebdab55e21cfd46bdb79b24c61d`.
The signed compressed SHA-256 is
`1c21c368d22abd6fe509bd185d8ba1408d703941b4fb2de8381333a9e64ddd00`;
the signed expanded SHA-256 is
`d8cb7101c6102c394620547d46f576ae965d326202ce9e24bdafe73283a0962f`.
The release key existed only on a disposable APFS RAM disk, its public identity
and signature were verified on macOS and `anima`, and the RAM disk was ejected.
The dedicated 63.9 GB USB card was then written, flushed, completely read back,
matched the signed expanded digest, and safely ejected. Exact media evidence is
in `evidence/recovery-media-phone001-c9b01fb.json`. The candidate remains
unapproved until physical boot acceptance.

Source commit `4a15bf63c9d4a44532d015f8212d44a2a987e7c1` supersedes the
software candidates above. Its factory-seeded 15,636,365,312-byte image has
SHA-256 `277044c2d3e2332c07aa7849329ba2589a45136d1eb5110812218c12fd75f8ef`.
The commit-bound local ledger, successful CI run `34346753149`, exact-image
boot, independent captive-portal client, restricted Wi-Fi credential handoff,
external update and maintenance channels, virtual peripheral faults,
disk-exhaustion behavior, abrupt power cuts, and checkpoint recovery all
passed the executable release gate. Evidence is retained on `anima` at
`/data2/millennium-build-4a15bf6/repo/tools/qemu/state/artifacts/full-20260909T115901Z/`.
The factory-seeded image was compressed into the canonical recovery format and
signed with the active `release-2026-08` key recovered only on a temporary APFS
RAM disk. Its public DER identity matched the established trust root; the
signature and full Zstandard expansion verified independently on macOS and
`anima`, and the RAM disk was ejected. Exact hashes are in
`evidence/zero2w-recovery-seeded-artifact-4a15bf6-2026-09-09.json`. A new
non-overwriting copy on external `UEBuild` matched all four artifact hashes,
the public-key identity and signature, and the complete expanded stream without
using laptop internal storage. This is automated evidence only. The image
remains explicitly unapproved until it passes the physical-only gates.

On 2026-09-09 macOS identified the dedicated 63.9 GB removable USB card as
whole disk `/dev/disk12`, distinct from the 512 GB `UEBuild` source. The media
writer reverified the canonical signature and compressed artifact, wrote and
flushed all 15,636,365,312 bytes, and hashed the complete image length back.
Readback matched signed expanded SHA-256
`277044c2d3e2332c07aa7849329ba2589a45136d1eb5110812218c12fd75f8ef`.
Spotlight briefly dissented from the writer's first eject request; a subsequent
eject succeeded after the indexing process released the volume. Evidence is in
`evidence/recovery-media-phone001-4a15bf6.json`. Physical boot acceptance
remains open.

The read-back-verified `4a15bf6` card was subsequently rejected during physical
boot. `daemon.service` failed with systemd `203/EXEC` and `Permission denied`:
the image installed `/opt/millennium` as `root:root 0750`, preventing the
unprivileged `millennium` service user from traversing the executable path.
The earlier exact-image result was a false positive because the harness matched
a marker substring echoed by systemd before its assertions ran. Corrective
commits require an exact standalone result marker, boot both system partitions,
execute the production daemon as user `millennium`, reject `203/EXEC`, and
normalize `/opt` and `/opt/millennium` to `root:root 0755`.

Corrected image-layer source `1f637722b3d2403308392e40912da2c5831ec5e7`
was rebuilt and factory-seeded on `anima`; the unchanged application payload is
from `4a15bf63c9d4a44532d015f8212d44a2a987e7c1`, and the final test harness is
`ff277828c6bf0b3f7bb6b6a95294e836c53e1c0a`. The 15,636,365,312-byte image has
SHA-256 `aa1b2b0b1d5b312709666db22d6246ae0cfd921ba13cce595a59893ed5142e71`.
A fresh full QEMU lab passed both exact image slots, product-daemon execution,
independent captive-portal credential handoff, virtual peripherals and injected
faults, signed OTA and external-origin recovery, maintenance-tunnel recovery,
signed experience lifecycle, disk exhaustion, network isolation, abrupt power
cuts, and checkpoint restore. The full result SHA-256 is
`09d8e2ab11695b2f09bdc0bcbae4d4a2aa7e64bc764649285ded6e8d7cf552ee`.
Automated evidence explicitly records `physical_hardware_claimed: false`.

The canonical corrected recovery package is signed by `release-2026-08`; the
private key was recovered only on a temporary APFS RAM disk, its public identity
matched the established trust root, the signature and complete expansion were
verified independently on macOS and `anima`, and the RAM disk was ejected. The
compressed SHA-256 is
`cb86f7347c76b25fc412c05e4830a0eef5dea39f4c2e16203fdc5bd799b5705a`.
The signed package was copied directly from `anima` to a new non-overwriting
directory on external `UEBuild`. All four stored hashes, the canonical
signature, the public-key identity, and the complete 15,636,365,312-byte
expanded stream matched without using laptop internal storage. Exact automated
evidence is in
`evidence/zero2w-recovery-seeded-artifact-1f63772-2026-09-09.json`.

Physical captive onboarding of the `1f63772` image exposed one further defect:
the setup firewall dropped established reply traffic arriving on `wlan0`, so
the phone could associate and obtain DHCP yet the helper's phone-originated
connectivity check failed and rolled credentials back. Commit
`c4092377623f06d67ebaa942283108fb512dfc7a` accepts established/related replies
before denying setup-client initiation, and adds a real network-namespace,
veth, and nftables packet-path regression test. The exact factory-seeded image
passed both system slots and the complete QEMU release lab on `anima`, including
the independent captive client, OTA and maintenance paths, virtual peripherals,
fault injection, and power/checkpoint recovery. It was signed on a disposable
APFS RAM disk with the active key, verified independently on `anima`, copied
directly to external `UEBuild`, and its complete expanded stream was verified
there. Evidence is in
`evidence/zero2w-recovery-seeded-artifact-c409237-2026-09-10.json`. The image is
still unapproved pending a full media write/readback and physical boot.

On 2026-09-10 macOS identified the dedicated recovery card as removable USB
whole disk `/dev/disk10`, 63,864,569,856 bytes, distinct from the 512 GB fixed
`UEBuild` device. The writer reverified the signature and exact `c409237`
provenance, wrote and flushed all 15,636,365,312 expanded bytes, and hashed the
complete image length back. Readback matched signed expanded SHA-256
`f74ca9e0b757dbda0be45f430a0b179c5d1808f2d96da96527bb2e5bed3e4a45`.
Spotlight initially dissented from eject after verification; a subsequent eject
succeeded. Evidence is in `evidence/recovery-media-phone001-c409237.json`.
Physical boot acceptance remains open.

On 2026-09-10 macOS identified the dedicated recovery card as whole removable
USB disk `/dev/disk10`, 63,864,569,856 bytes, distinct from the 512 GB fixed
`UEBuild` device. The writer reverified the canonical signature and compressed
artifact, wrote and flushed all 15,636,365,312 expanded bytes, and hashed the
complete image length back. Readback matched signed expanded SHA-256
`aa1b2b0b1d5b312709666db22d6246ae0cfd921ba13cce595a59893ed5142e71`.
The card was safely ejected. Evidence is in
`evidence/recovery-media-phone001-1f63772.json`; physical boot acceptance
remains open.
