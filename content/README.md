# Story content

Stories are data, not daemon code. Each story directory contains `story.json`
and an optional `media/` directory. A scene gives the two display lines, an
optional WAV filename, and event-to-scene transitions. Supported events cover
keypad input, coins, credentials, handset changes, timeouts, resumed sessions,
and a default recovery path.

Run these before review or deployment:

```sh
python3 content/storytool.py validate content/stories/last_line/story.json
python3 content/storytool.py explore content/stories/last_line/story.json
python3 content/storytool.py preview content/stories/last_line/story.json
python3 content/storytool.py compile content/stories/last_line/story.json \
  --output content/stories/last_line/story.mst
python3 content/storytool.py package content/stories/last_line/story.json \
  --output build/content --private-key /secure/content-signing.pem --key-id content-2026
python3 content/catalogtool.py build build/content/*.manifest.json \
  --base-url https://updates.kmatzen.com/experiences/ \
  --channel stable --sequence 1 --key-id catalog-2026 \
  --private-key /secure/catalog-signing.pem --output build/content/catalog.json
python3 content/catalogtool.py validate build/content/catalog.json
python3 content/catalogtool.py select build/content/catalog.json \
  --device-id phone-001
python3 content/publish_catalog.py build/content \
  /home/kmatzen/selfhosted/millennium-updates/www/experiences \
  --catalog-key catalog-2026:/secure/catalog-public.pem \
  --package-key content-2026:/secure/content-public.pem
```

Validation rejects unreachable scenes, unmarked dead ends, closed loops with no
ending, missing or unused media, duplicate inputs, invalid transitions, and
copy that exceeds the physical 20-character VFD. It also rejects undeclared
runtime capabilities, invalid compatibility ranges, ratings/locales, state
migrations, and package quotas. `preview` lets an editor play each choice locally;
`explore` prints every acyclic path and ending for review. Packaging produces a
canonical schema-2 manifest, per-file inventory and digests, compressed size,
monotonic sequence, compatibility/capability/rating/quota policy, reproducible
archive, and optional Ed25519 signature. Content versions are independent of
the daemon version; sequence provides the anti-rollback ordering.

On the phone, `install_content.py install` requires that signature and a
matching trusted key ID, revalidates the author and runtime forms, installs an
immutable release, and atomically moves `current` while retaining `previous`.
It accepts legacy schema-1 releases during migration, but schema-2 releases
also enforce archive and expanded-size limits, exact file inventory, runtime
and daemon compatibility, allowlisted capabilities, and persistent monotonic
sequence state. A rollback switches to the retained known-good release without
lowering that anti-rollback state.

`catalogtool.py` builds canonical signed channel catalogs from schema-2 package
manifests. Catalog entries use immutable HTTPS manifest/signature URLs and bind
the manifest SHA-256. Deterministic per-device rollout supports percentages,
device groups, global holds, signed-digest withdrawal, package-ID denial, and
installed-sequence filtering. The website remains an untrusted transport;
catalog and package signatures establish authority.

`publish_catalog.py` verifies both signature layers and every bound digest
again at the server boundary. It refuses an existing release directory or a
non-increasing channel sequence, publishes packages beneath
`releases/<id>/<sequence>-<version>/`, then atomically commits the mutable
channel catalog last. The publisher accepts public verification keys only;
offline private keys never belong on the update server.

On `anima`, `updates.kmatzen.com` is served by the unprivileged
`millennium-updates` container. Its read-only `/srv/updates` mount is backed by
the operator-writable host directory
`/home/kmatzen/selfhosted/millennium-updates/www`; run the publisher against
that host path. Copy only the already-signed package set, this publisher and
its Python verifier modules, and the public keys to a private staging
directory. No container restart is required because nginx reads the bind mount
for each request. Verify the public catalog URL and every catalog-bound object
after publication before advancing rollout.

The full-device OTA bundle carries the matching content verifier and compiler
under `/opt/millennium/current/content`, so a story compiler-format change is
promoted with the daemon that reads it. Production content activation should
invoke that release-owned verifier rather than an older fixed-path copy.
`install_content.py rollback` swaps those links without changing the host
daemon or MCU firmware. Configure `story.path` to
`/var/lib/millennium/content/current/story.mst`.

`make -C host install` installs the verifier as
`/usr/local/libexec/millennium-content` and provisions *The House at the End of the Line* as an
immutable offline bootstrap release. It never replaces an existing `current`
content link. New content can therefore be installed and rolled back without
replacing the daemon, while a factory-new or disconnected phone still has a
complete local experience.

Authors should keep the first prompt actionable, acknowledge every input with
audio or display feedback, provide timeout and hang-up recovery, make the main
ending discoverable without secrets, and test every branch with a first-time
caller. Never place personal data in story state or analytics.

The runtime supports conditional transitions over persistent integer variables,
`set` and `increment` actions, the special `$hour` and `$weekday` variables,
outgoing calls and call-state transitions, per-scene `timeout_seconds`, and
events for every physical input. A scene may also schedule one persistent local
callback after a bounded wall-clock delay:

```json
"callback": {"after_seconds": 86400, "target": "operator_callback"}
```

The due timestamp survives daemon restarts. Story Mode delivers the callback
only while the handset is down, records it as consumed before entering the
target, and can route a timeout into an ordinary message-waiting scene. This is
a narrative callback, not permission to call a real telephone number; external
calls still use the separately configured `call` field.

Operators may set `story.callbacks_enabled=false` as an immediate kill switch.
`story.callback_quiet_start` and `story.callback_quiet_end` are local hours
(defaults `22` and `8`); a due callback remains pending until the handset is
down, callbacks are enabled, and the current hour is outside that interval.

Accessibility metadata configures output
volume, a global repeat key, minimum response time, high-contrast copy, and a
spoken-instruction mode that requires audio for every actionable scene.
Audio values are safe `.wav` filenames stored in the story's `media/`
directory; the runtime resolves them through the atomically selected content
release, so media changes and rollbacks cannot drift away from story data.

Editorial and release criteria live in [STORY_BIBLE.md](STORY_BIBLE.md), the
planned experience sequence in [ROADMAP.md](ROADMAP.md), and first-time-caller
observation records in [PLAYTEST.md](PLAYTEST.md).
