# Downloadable experiences and plugin lifecycle

## Objective

Permit the phone to gain new and improved experiences throughout its installed
life without reflashing its SD card or requiring its owner to administer Linux.
Packages are published through `updates.kmatzen.com`, discovered automatically,
installed safely while the phone is idle, and reversible without local access.

## Current boundary

The daemon's C plugins are compiled into the daemon and registered at startup.
They can already be replaced through a signed application OTA release, but they
cannot currently be installed individually at runtime. Narrative source, media,
and other content can already be independently versioned, signed, validated,
activated, and rolled back.

That distinction is intentional. Downloading arbitrary native code would give a
package the daemon's hardware and filesystem authority. Most future experiences
should therefore be data-driven packages executed by a constrained experience
runtime. Native C plugins remain part of full daemon releases when a feature
genuinely needs a new hardware-level primitive.

## Distribution model

The update website exposes two signed layers:

1. A small channel catalog describes the latest eligible version of each
   experience and supports stable, beta, device-group, hold, withdrawal, and
   percentage rollout controls.
2. Each catalog entry points to an immutable HTTPS package. The phone verifies
   the catalog signature, package digest, package signature, compatibility,
   size, and policy before extraction.

The catalog is not an app store running on the public web. The website is an
untrusted transport: authenticity comes from offline-controlled signing keys,
and a compromised server must not be able to install unsigned content or roll
the phone back to a vulnerable package.

## Package contract

Every signed package manifest includes:

- stable plugin ID, display name, package version, and monotonic sequence;
- minimum and maximum daemon/runtime schema versions;
- required phone capabilities and optional peripheral fallbacks;
- content rating, intended audience, locale, and accessibility metadata;
- entry points, interaction mappings, and resumable-state schema;
- a complete file list with SHA-256 digests and an overall size limit;
- signing key ID, channel, rollout policy, and withdrawal identity;
- storage, session-time, audio, display, and persistent-state quotas;
- state-migration and rollback compatibility information.

Packages may contain validated story graphs, prompts, recorded speech, sound
effects, display sequences, rules, localization, and bounded package state.
They may invoke only versioned runtime capabilities such as handset events,
keypad input, purpose-built cards, coin events, display output, approved audio,
timers, and namespaced persistence. They cannot execute shell commands, open
arbitrary network connections, load native libraries, access credentials, or
read another package's state.

## Installation lifecycle

The phone periodically checks the signed catalog with bounded jitter. It stages
eligible packages under a temporary name, safely extracts them, validates every
file and the complete experience graph, and runs a noninteractive preview and
compatibility check. Activation uses an atomic release pointer and occurs only
while the phone is idle. The prior known-good package remains available.

Startup health, a bounded synthetic interaction, and runtime error thresholds
form the health gate. A failed activation automatically restores the previous
package and quarantines the failing signed digest so it does not retry forever.
Power loss at download, extraction, validation, activation, state migration, or
garbage collection must leave either the old release or the complete new release
selectable. Storage pressure removes only inactive, unpinned releases and never
the active or sole rollback copy.

Catalog withdrawal prevents new installs. An emergency signed denylist can
disable a harmful package and return the phone to its configured fallback
experience. Key rotation and revocation follow the existing signing-key
lifecycle, with anti-rollback state stored outside individual package releases.

## Owner and maintainer experience

The inexperienced owner should not need to manage packages. Safe stable-channel
updates happen automatically and never interrupt a call. The local authenticated
dashboard may show installed experiences, available updates, active/fallback
selection, rating, storage use, last health result, and a simple enable/disable
control. Administrative catalog/channel changes remain available through the
restricted maintenance path.

Metrics are privacy-preserving: package/version health, activation outcome,
crash or validation category, completion/abandonment counts, and storage use.
They must not upload caller speech, card tokens, free-form caller input, or a
reconstructable interaction history.

## Delivery plan

### Phase 1 — Specify and validate

- Define signed catalog and package schemas and add them to compatibility
  metadata.
- Define the versioned runtime capability API, package quotas, ratings policy,
  and native-code prohibition.
- Extend the authoring tool to build deterministic packages and reject unsafe
  paths, missing media, invalid graphs, incompatible schemas, and excessive
  resource use.

### Phase 2 — Install and recover

- Implement catalog polling, eligibility, staged download, dual signature/hash
  verification, safe extraction, atomic activation, and retained rollback.
- Add power-loss recovery, digest quarantine, withdrawal/denylist handling,
  state migration, bounded garbage collection, and a permanent fallback.
- Prevent activation during calls, maintenance blackout periods, or unhealthy
  hardware state.

### Phase 3 — Runtime and management

- Execute packages through the constrained capability API with time, storage,
  event-rate, and media limits.
- Add authenticated owner views and restricted maintainer controls without
  exposing signing or system administration to the owner.
- Add privacy-preserving health and engagement telemetry.

### Phase 4 — Prove the lifecycle

- Add unit, schema-fuzz, malicious-archive, compatibility, and migration tests.
- Extend QEMU coverage through catalog compromise, signature failure, rollback,
  withdrawal, disk-full, network loss, daemon restart, and power interruption at
  every durable transition.
- Run real-phone acceptance for audio, display, all physical inputs, long-term
  storage behavior, owner usability, and staged production rollout.

## Release gates

No downloadable-experience release is complete until an unsigned, malformed,
incompatible, over-quota, withdrawn, or rolled-back package is rejected; every
interrupted transition recovers without manual access; a bad activation rolls
back and stops retrying; the fallback remains usable offline; and a first-time
owner can leave automatic updates enabled without learning an administrative
workflow.
