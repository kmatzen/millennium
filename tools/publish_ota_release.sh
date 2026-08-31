#!/usr/bin/env bash
set -euo pipefail

usage() { echo "usage: $0 RELEASE_OUTPUT_DIR WEB_ROOT" >&2; exit 2; }
[ "$#" -eq 2 ] || usage
SOURCE="$(cd "$1" && pwd)"
WEB_ROOT="$2"
MANIFEST="$SOURCE/manifest.json"
SIGNATURE="$SOURCE/manifest.json.sig"
[ -s "$MANIFEST" ] && [ -s "$SIGNATURE" ] || { echo "signed manifest is required" >&2; exit 1; }

META="$(python3 - "$MANIFEST" <<'PY'
import json, pathlib, sys
d = json.loads(pathlib.Path(sys.argv[1]).read_text())
print(("%08d-%s" % (d["sequence"], d["version"])) + " " + pathlib.PurePosixPath(d["bundle"]["url"]).name)
PY
)"
RELEASE_ID="${META%% *}"
BUNDLE_NAME="${META#* }"
BUNDLE="$SOURCE/$BUNDLE_NAME"
[ -s "$BUNDLE" ] || { echo "bundle not found: $BUNDLE" >&2; exit 1; }
python3 - "$MANIFEST" "$BUNDLE" <<'PY'
import hashlib, json, pathlib, sys
manifest = json.loads(pathlib.Path(sys.argv[1]).read_text())
bundle = pathlib.Path(sys.argv[2])
digest = hashlib.sha256(bundle.read_bytes()).hexdigest()
if bundle.stat().st_size != manifest["bundle"]["size"] or digest != manifest["bundle"]["sha256"]:
    raise SystemExit("bundle does not match signed manifest")
PY

# The bundle and signature become visible before the manifest. Renaming the
# manifest is the publication commit point observed by clients.
install -d -m 0755 "$WEB_ROOT/releases/$RELEASE_ID" "$WEB_ROOT/stable"
install -m 0644 "$BUNDLE" "$WEB_ROOT/releases/$RELEASE_ID/$BUNDLE_NAME"
install -m 0644 "$SIGNATURE" "$WEB_ROOT/stable/manifest.json.sig.new"
mv "$WEB_ROOT/stable/manifest.json.sig.new" "$WEB_ROOT/stable/manifest.json.sig"
install -m 0644 "$MANIFEST" "$WEB_ROOT/stable/manifest.json.new"
mv "$WEB_ROOT/stable/manifest.json.new" "$WEB_ROOT/stable/manifest.json"
echo "Published Millennium $RELEASE_ID"
