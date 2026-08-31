# OTA signing-key lifecycle and recovery

Production releases use offline Ed25519 private keys. Phones contain public
keys only. Every manifest names its key with `key_id`; `trusted_keys` may list
an old and new key during a deliberately bounded rotation window.

## Key ceremony and backup

1. On a disconnected maintenance machine, generate a new Ed25519 key and give
   it a unique date-based ID such as `release-2026-09`.
2. Export the public key. Record its SHA-256 fingerprint in the as-built and
   release records.
3. Make two encrypted backups of the private key on separate removable media.
   Encrypt to the recovery custodians' hardware-backed keys; never use a
   password stored beside the media. Store the two copies in different secure
   locations. The working private key must not be copied to the update server,
   phone, repository, or ordinary cloud storage.
4. Mount each backup read-only on a second offline machine, decrypt into a
   memory-backed temporary directory, sign a disposable manifest, verify it
   with the recorded public key, then erase the temporary plaintext. Record
   the date, operators, key ID, media IDs, and successful signature digest.

`tools/signing_key_backup.py` implements both halves without ever putting the
plaintext key in the repository or update server. `backup` verifies that the
private key matches the recorded public key, requires a full OpenPGP recipient
fingerprint, and records the ciphertext digest. `recover` requires an
operator-supplied memory-backed scratch directory, decrypts there, signs and
verifies a disposable challenge, removes the plaintext, and writes a dated
evidence record. Store that record in the private operations log.

## Rotation

1. Provision the new public key alongside the old one on every phone through a
   release signed by the old key.
2. Verify fleet monitoring shows the new key ID in `trusted_keys` everywhere.
3. Publish one canary release signed by the new key, then expand its device
   groups. Keep the old public key during the rollback window.
4. Publish a new-key-signed release that removes the old public key. Confirm
   adoption before treating the old key as retired.

## Revocation and emergency recovery

If a private key may be compromised, immediately withdraw its latest manifest,
hold automatic rollout, and publish a higher-sequence recovery release using a
previously provisioned emergency key. That release removes the compromised
public key. Never lower the sequence or reuse a release identity. Devices that
do not trust the emergency key require physical recovery; do not bypass
signature verification.

If all online signing capability is lost, restore one encrypted backup on the
offline machine and perform the disposable-signature check before issuing a
release. If the backup cannot be verified, stop: the safe recovery is physical
reprovisioning with a newly generated trust root.

The checklist item is complete only when the encrypted media exists and a
dated restore/sign/verify drill record is committed to the private operations
log. This document alone is not evidence that a backup or drill happened.
