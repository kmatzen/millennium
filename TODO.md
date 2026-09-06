# Millennium Phone TODO

This list captures the work required to make the phone safe and maintainable
as an unattended appliance. Items are ordered by priority.

The remaining physical and human acceptance work is sequenced in
`hardware/as-built/phone-001/NEXT_PHYSICAL_SESSION.md` so it can be completed
with one assembled-hardware session and without repeating the network wiring.

## P0 — Required before handoff

- [ ] Add owner-friendly Wi-Fi onboarding and recovery.
  - [x] On first boot with no configured network, create a temporary
    `Millennium-Setup-<device-id>` WPA2 access point and captive portal.
  - [x] Generate a unique per-device setup password at provisioning time and
    print the SSID, password, and portal URL as a QR code in the owner packet.
    Never use a fleet-wide password.
  - [x] Let the owner scan for nearby networks, enter credentials over the
    local setup link, and see clear connecting/success/failure feedback.
  - [x] Store Wi-Fi credentials only in root-owned NetworkManager connection
    profiles; never log, back up, export, or expose them to the daemon.
  - [x] Validate and escape SSIDs and passphrases without invoking a shell.
    Support spaces, Unicode, hidden SSIDs, WPA2/WPA3 transition networks, and
    open networks only after an explicit warning.
  - [x] Shut down the setup AP after a verified station connection, restart
    OTA and maintenance services, and confirm the phone reaches its update and
    maintenance endpoints.
  - [x] Provide a protected physical recovery gesture that reopens setup for
    15 minutes without erasing the existing network; never enter setup merely
    because Internet access is temporarily unavailable.
  - [x] Isolate setup mode with firewall rules: permit only DHCP, DNS, and the
    portal from the setup interface; block SSH, the admin API, forwarding, and
    access to stored credentials.
  - [x] Add attempt throttling, a visible setup-mode indicator, a hard timeout,
    and automatic rollback to the previous working network after failure.
  - [ ] Test wrong passwords, hidden networks, power loss during save, radio
    failure, captive-portal detection on iOS/Android/macOS/Windows, and recovery
    with local console access before enabling it on a deployed phone.
    - [x] Automated fault tests cover wrong credentials, hidden profiles,
      atomic-save interruption, radio errors, and platform probe routes.
    - [x] Export privacy-preserving physical-client evidence only after the
      browser family matches a recently observed native captive probe.
    - [ ] Repeat the matrix with physical radios, each named client platform,
      and a local console on the production phone.
  - Done when a first-time owner can connect the phone to a new home network
    from a mobile browser without maintainer help, while an untrusted setup
    client cannot reach administrative services or recover stored credentials.

- [ ] Add fail-safe full operating-system OTA updates.
  - [x] Build a supported NetworkManager-based Raspberry Pi OS image for the
    Zero 2 W with redundant boot/root slots and a separate persistent-data
    partition; do not repartition the deployed Bullseye system in place.
  - [x] Use the Raspberry Pi firmware's one-shot `tryboot` A/B mechanism so an
    uncommitted image automatically falls back after a failed or interrupted
    boot without requiring network access.
  - [x] Authenticate the OS image manifest and every compressed and expanded
    boot/root payload; bind model, partition layout, source commit,
    monotonically increasing sequence, minimum compatible application/MCU
    versions, rollout controls, and exact sizes and hashes into the signature.
  - [x] Stream verified images only to inactive slots, read them back, and
    reject overlap with the active boot/root devices before changing boot state.
  - [x] Preserve per-device identity, NetworkManager profiles, setup secret,
    maintenance credentials, story state, logs required for diagnosis, and OTA
    anti-rollback state outside the replaceable root filesystems.
    - [x] Make the image-owned shared-state generator link every declared mount
      into `local-fs.target`, and regression-test the generated dependencies.
    - [x] Reject non-canonical machine IDs and provision the daemon token with
      the exact ownership and group-readable mode required at runtime.
    - [ ] On the rebuilt physical image, prove `/` is read-only and every
      declared `/etc`, `/var`, and identity path is writable from partition 7
      before accepting daemon, Wi-Fi, SSH, or OTA health.
  - [ ] Commit the new boot slot only after bounded checks prove the daemon,
    both MCUs, audio, SIP, local controls, update endpoint, and maintenance
    tunnel are healthy; otherwise record failure and return to the prior slot.
    - [x] Implement and software-verify the bounded gate, including an actual
      ALSA PCM write, fresh independent Alpha/Beta diagnostics, named daemon
      health, and reverse-tunnel stability beyond its SSH failure window.
    - [ ] Boot the signed candidate on the production-equivalent Zero 2 W and
      prove a healthy physical candidate commits while each failed gate returns
      to the prior slot.
  - [ ] Rate-limit and quarantine failed OS images, respect calls and
    maintenance windows, expose owner-safe status, and retain a signed recovery
    image that can be written by a maintainer.
    - [x] Implement digest-scoped exponential backoff, bounded retry quarantine,
      busy-device and maintenance-window gates, administrative clearing, and
      privacy-safe owner status.
    - [x] Produce, sign, and retain a software-verified production-equivalent
      recovery image, with its exact compressed and expanded hashes bound to
      the active release key. Keep it explicitly unapproved until physical boot.
    - [ ] Write the retained image to OS-identified removable recovery media and
      verify its full image-length readback against the signed expanded digest.
    - [ ] Boot a non-quarantined verified recovery image on the
      production-equivalent Zero 2 W; the physically booted v3 artifact is
      retained as failed evidence and must not be redeployed.
  - [ ] Exercise download, inactive-slot write, pre-reboot, first boot, health
    commit, and rollback in QEMU and by physically removing power on the
    production-equivalent Zero 2 W image.
    - [x] Exercise the complete signed OS state machine in the ARM64 QEMU guest,
      including interrupted/truncated download, failed-candidate fallback,
      quarantine, retry, and healthy commit without claiming Pi firmware
      fidelity.
    - [ ] Repeat the matrix on the production-equivalent Zero 2 W image with
      measured physical power removal at every journal boundary.
  - [ ] Reimage the production phone only from locally attached recovery media,
    restore its per-device state, and prove both A/B directions plus recovery
    before handoff.
  - Done when a bad, truncated, or power-interrupted OS release cannot replace
    the last bootable slot and a healthy signed OS update commits unattended.

- [x] Secure the management API.
  - [x] Bind administrative endpoints to loopback or a Unix socket by default.
  - [x] Expose remote administration only through the authenticated maintenance path.
  - [x] Require a per-device credential or mutual TLS for state-changing requests.
  - [x] Add origin/CSRF checks, rate limiting, and firewall rules.
  - [x] Separate read-only status endpoints from administrative controls.
  - Done when an unauthenticated LAN client cannot control the phone or start an update.

- [x] Make OTA release storage immutable.
  - [x] Key release directories by sequence and version, not version alone.
  - [x] Refuse to overwrite a release referenced by `current` or `previous`.
  - [x] Reject duplicate or conflicting release identities.
  - [x] Add tests for version reuse, interrupted activation, and rollback.
  - Done when no valid manifest can destroy the active or rollback release.

- [x] Establish a single authoritative software version.
  - [x] Generate the daemon and OTA manifest versions from one source.
  - [x] Have the bundle builder execute the packaged daemon's `--version`.
  - [x] Refuse to sign a release when the embedded and requested versions differ.
  - [x] Keep documentation and compatibility metadata generated from the same version.
  - Done when a version mismatch cannot enter a signed bundle.

- [x] Fix PJSIP shared-state concurrency.
  - [x] Serialize SIP state changes or protect all shared state with synchronization.
  - [x] Snapshot and validate call IDs before answer or hangup operations.
  - [x] Copy `pj_str_t` values by length into bounded, NUL-terminated buffers.
  - [x] Add concurrency and malformed-string tests.
  - Done when SIP callbacks and daemon threads have no unsynchronized shared access.

- [x] Honor configured serial devices and baud rates.
  - [x] Pass parsed hardware configuration into the serial client.
  - [x] Remove hardcoded Beta-device assumptions.
  - [x] Add tests covering non-default paths and reconnects after USB renumbering.
  - Done when both MCU paths can be changed solely through configuration.

- [x] Attest both MCU firmware images after OTA flashing.
  - [x] Make each MCU report its role, firmware version, build hash, and protocol version.
  - [x] Put the expected identities in the signed release manifest.
  - [x] Verify the display and keypad independently before committing an update.
  - [x] Roll back if either MCU fails identity or health validation.
  - Done when swapped, stale, or incorrect firmware cannot pass the OTA health gate.

- [x] Quarantine failed OTA releases.
  - [x] Record failures by signed-manifest digest and sequence.
  - [x] Add exponential backoff and a maximum retry count.
  - [x] Retry only after a newer signed release or an explicit administrative clear.
  - [x] Test repeated download, flash, health failure, and rollback behavior.
  - Done when a bad release cannot repeatedly disrupt or reflash the phone.

## P1 — Production hardening

- [x] Harden the daemon's systemd sandbox.
  - [x] Remove unnecessary capabilities and unlimited memory locking.
  - [x] Use an unprivileged port or systemd socket activation.
  - [x] Enable filesystem, home, temporary-directory, address-family, and device restrictions.
  - [x] Replace the external ping startup gate with offline-capable retry behavior.
  - [x] Validate the unit with `systemd-analyze security` and `systemd-analyze verify`.

- [x] Remove the Git-based updater from production builds.
  - [x] Compile it only in an explicitly selected development mode, or delete it.
  - [x] Never fall back from signed OTA when the worker or trust key is unavailable.
  - [x] Replace remaining shell-interpolated commands with argument-vector execution.

- [x] Add signing-key lifecycle support.
  - [x] Support key IDs and multiple trusted public keys during rotation.
  - [x] Document revocation and emergency recovery.
  - [x] Maintain two currently verified encrypted offline copies of the private
    signing key on separate media, as required by the lifecycle procedure.
    - [x] Provide an OS-verified removable-media copier that rejects internal
      disks and overwrites, verifies the ciphertext, and records media custody.
    - [x] Create, recovery-test, and eject the first encrypted removable-media
      copy of the active `release-2026-08` key (`SD-EEE0E3AE`, 2026-09-06).
    - [x] Repeat the verified copy and recovery drill on a second physically
      distinct removable device (`NVME-68D6C050`, 2026-09-06).
  - [x] Perform and document a recovery drill.

- [x] Expand CI into a reproducible release pipeline.
  - [x] Link and test against a pinned PJSIP version.
  - [x] Compile both Arduino sketches with pinned tools and libraries.
  - [x] Rebuild and compare checked-in firmware artifacts.
  - [x] Run host code under AddressSanitizer and UndefinedBehaviorSanitizer.
  - [x] Add OTA install, activation, health-check, and rollback integration tests.
  - [x] Add `shellcheck` and systemd unit validation.
  - [x] Run PCB checks with a pinned KiCad environment.
  - [x] Publish checksums, source commit, tool versions, and an SBOM with releases.

- [x] Add operational monitoring and alerts.
  - [x] Monitor last check-in, SIP registration, and maintenance-tunnel availability.
  - [x] Alert on OTA failure or rollback, serial drops, and MCU resets.
  - [x] Monitor disk space, filesystem errors, certificate expiry, and reboot frequency.
  - [x] Back up OTA artifacts, server configuration, and authorized maintenance keys.
    - [x] Stream update-server and doormand state into encrypted Restic and
      verify every saved archive through a restore stream before success.

- [ ] Validate the full unattended workflow on real hardware.
  - [x] Install a release from `kmatzen.com` using the normal timer path.
  - [x] Confirm both MCUs, the host daemon, audio, SIP, and controls after activation.
  - [x] Force a failed health check and verify automatic rollback.
  - [ ] Test loss of power and network during download, flash, and activation.
    - [x] Automated fault injection proves atomic download, selective MCU-flash
      recovery, and activation-journal rollback.
    - [x] Provide a durable physical-interruption harness that captures the
      before/after boot, release, content, firmware, OTA, service, and HIL state.
    - [x] Physically interrupt Ethernet during an authenticated production-signed
      bundle download, retain client/relay/harness evidence, restore the link,
      and verify the complete bundle plus unchanged release state and full HIL.
    - [ ] Repeat the interruptions by physically removing power/network on the
      production phone for the remaining power, MCU-flash, and activation cases,
      and attach journal and measurement evidence.
  - [ ] Confirm remote maintenance works from outside the home network.
    - [x] Confirm the reverse tunnel, one-minute metrics pull, and nightly
      restricted backup pull remain operational through anima.
    - [x] Verify an approved hardware-backed administrator key through the
      reverse tunnel; the explicit FIDO identity succeeded without phone changes.
    - [x] Provide a privacy-preserving external-vantage audit that rejects the
      home source network and verifies hardware-backed login and phone health.
    - [ ] Complete an interactive maintenance session from a genuinely
      external network and retain the dated audit evidence.
  - [x] Provide a machine-validated final handoff record that binds the real
    Wi-Fi client matrix, external maintenance evidence, two distinct offline
    key copies, physical as-built evidence, and first-time-caller playtests.

## P2 — Reliability and maintainability

- [ ] Build richer interactive experiences and storytelling.
  - [x] Define the phone's core experience loop: invitation, interaction, response, consequence, and return visit.
  - [x] Create a narrative system for branching calls, timed events, recurring characters, secrets, and persistent story state.
  - [x] Let stories react to handset state, keypad input, coins, cards or tokens, time of day, prior choices, and interrupted calls.
  - [x] Support graceful recovery when a caller hangs up, times out, enters unexpected input, or returns later.
  - [x] Add clear audio and display feedback so callers always understand what actions are possible and whether input was accepted.
  - [x] Provide reusable pacing tools for prompts, pauses, repetition, escalation, callbacks, and satisfying endings.
  - [x] Build authoring and validation tools so new stories can be created without modifying daemon code.
  - [x] Add a local preview or simulation mode for playing through every branch before deployment.
  - [x] Validate story graphs for unreachable scenes, dead ends, missing media, invalid transitions, and infinite loops.
  - [x] Make content packages independently versioned, signed, updateable, and reversible through OTA.
  - [x] Add accessibility options for volume, prompt repetition, response timing, spoken instructions, and display legibility.
  - [ ] Conduct playtests with first-time callers and record where they become confused, disengage, or fail to discover an interaction.
    - [x] Provide privacy-preserving guided participant, resilience-scenario,
      and release-decision commands with tamper-checked evidence files.
  - [x] Collect privacy-preserving experience metrics such as completion, abandonment, retries, branch selection, and session duration.
  - [x] Document the intended tone, characters, world rules, interaction vocabulary, and content-review standards.
  - Done when a non-developer can author and preview a branching experience, a first-time caller can complete it without coaching, interrupted sessions recover coherently, and content can be safely deployed or rolled back independently.

- [x] Create an initial story and interaction roadmap.
  - [x] Ship one polished short experience that demonstrates calls, keypad choices, audio, display feedback, and persistent consequences.
  - [x] Add at least one experience that rewards a return visit or changes over real-world time.
  - [x] Add optional discoveries that use the physical phone hardware without blocking the main story.
  - [x] Establish a small library of reusable voices, sound cues, transitions, and interaction patterns.
  - [x] Define content ratings and safeguards appropriate for the phone's expected audience and location.
  - [x] Keep a fallback experience available when the network or external services are unavailable.

- [x] Convert long Arduino operations to nonblocking state machines.
  - [x] Add deadlines and transaction IDs to VFD and coin-validator operations.
  - [x] Reset the watchdog only when forward progress is made.
  - [x] Report the previous MCU reset cause at startup.

- [x] Frame the MCU communication protocol.
  - [x] Add packet length, message type, sequence number, and CRC.
  - [x] Add acknowledgements or replay handling for critical events.
  - [x] Define protocol-version negotiation and compatibility behavior.

- [x] Replace payment-card identifiers with purpose-built credentials.
  - [x] Provision random tokens or keyed hashes instead of real card numbers.
  - [x] Clear sensitive buffers after use.
  - [x] Document prohibited configuration and logging data.

- [ ] Create an as-built record for every physical phone.
  - [x] Provide guided commands that hash installed artifacts, photographs,
    physical-test evidence, and enforce voltage measurements where required.
  - [ ] Record PCB revision, source tag, Gerber/BOM hashes, and installed firmware hashes.
  - [ ] Record device serial numbers, wiring deviations, and assembly photos.
  - [ ] Measure power and brownout behavior with the complete installed hardware.
  - [ ] Test safe recovery after arbitrary power loss.

- [x] Remove stale addresses and deployment procedures.
  - [x] Replace hardcoded `192.168.86.145` and `192.168.86.152` values with configuration or a stable hostname.
  - [x] Separate developer deployment, factory provisioning, production OTA, and recovery documentation.
  - [x] Mark all `git pull` deployment instructions as development-only.
  - [x] Correct the stale I2C address note in `Arduino/PINOUT.md`.
  - [x] Reconcile fabricated-board and future-revision notes in `pcb/README.md`.

- [x] Add staged rollout controls.
  - [x] Support device groups or channels, rollout holds, and explicit release withdrawal.
  - [x] Respect active calls and maintenance blackout periods.
  - [x] Add a nightly hardware-in-the-loop smoke test when practical.

## Release acceptance checklist

A release is ready for an inexperienced end user only when:

- [x] Administrative controls are inaccessible without authentication.
- [x] The host and both MCU images are authenticated and attested.
- [x] A successful OTA has completed through the scheduled production path.
- [x] A deliberately bad OTA has rolled back without manual intervention.
- [x] Failed releases stop retrying automatically.
- [ ] Power and network interruption tests recover safely.
- [x] Remote maintenance works through the known domain without inbound home-network access.
- [x] Monitoring reports the phone's health and alerts on loss of contact.
- [x] Signing keys, server state, and recovery instructions have currently
  verified tested backups.
- [ ] A first-time caller can discover and complete the primary experience without instruction from the owner.
- [ ] Story interruption, timeout, repeat-play, offline, and return-visit paths have been playtested.
- [x] Narrative content can be previewed, validated, deployed, and rolled back independently of daemon code.
