#!/usr/bin/env bash
set -euo pipefail

usage() { echo "usage: $0 RELEASE_OUTPUT_DIR WEB_ROOT PUBLIC_KEY" >&2; exit 2; }
[ "$#" -eq 3 ] || usage
SOURCE="$(cd "$1" && pwd)"
WEB_ROOT="$2"
PUBLIC_KEY="$3"
MANIFEST="$SOURCE/manifest.json"
SIGNATURE="$SOURCE/manifest.json.sig"

[ -s "$MANIFEST" ] && [ -s "$SIGNATURE" ] && [ -s "$PUBLIC_KEY" ] || {
    echo "signed manifest and public key are required" >&2
    exit 1
}
openssl pkeyutl -verify -rawin -pubin -inkey "$PUBLIC_KEY" \
    -in "$MANIFEST" -sigfile "$SIGNATURE" >/dev/null

META="$(python3 - "$MANIFEST" "$SOURCE" <<'PY'
import gzip, hashlib, json, pathlib, sys

manifest_path, source = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
value = json.loads(manifest_path.read_text())
if value.get("schema") != 1 or value.get("kind") != "millennium-os-release":
    raise SystemExit("not a Millennium OS release manifest")
release_id = "%08d-%s" % (value["sequence"], value["version"])
names = []
for role in ("boot", "root"):
    metadata = value["images"][role]
    name = pathlib.PurePosixPath(metadata["url"]).name
    path = source / name
    compressed = hashlib.sha256()
    expanded = hashlib.sha256()
    compressed_size = 0
    expanded_size = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            compressed.update(chunk)
            compressed_size += len(chunk)
    if (compressed_size != metadata["compressed_size"] or
            compressed.hexdigest() != metadata["compressed_sha256"]):
        raise SystemExit(role + " image does not match signed manifest")
    with gzip.open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            expanded.update(chunk)
            expanded_size += len(chunk)
    if (expanded_size != metadata["expanded_size"] or
            expanded.hexdigest() != metadata["expanded_sha256"]):
        raise SystemExit(role + " image does not match signed manifest")
    names.append(name)
print(release_id + " " + " ".join(names))
PY
)"
read -r RELEASE_ID BOOT_NAME ROOT_NAME <<<"$META"
RELEASE_DIR="$WEB_ROOT/releases/$RELEASE_ID"
[ ! -e "$RELEASE_DIR" ] || { echo "release already exists: $RELEASE_DIR" >&2; exit 1; }

# Immutable payloads become visible first. The manifest rename is the commit
# point; any manifest/signature race fails closed at signature verification.
install -d -m 0755 "$RELEASE_DIR" "$WEB_ROOT/stable"
install -m 0644 "$SOURCE/$BOOT_NAME" "$RELEASE_DIR/$BOOT_NAME"
install -m 0644 "$SOURCE/$ROOT_NAME" "$RELEASE_DIR/$ROOT_NAME"
install -m 0644 "$SIGNATURE" "$WEB_ROOT/stable/manifest.json.sig.new"
mv "$WEB_ROOT/stable/manifest.json.sig.new" "$WEB_ROOT/stable/manifest.json.sig"
install -m 0644 "$MANIFEST" "$WEB_ROOT/stable/manifest.json.new"
mv "$WEB_ROOT/stable/manifest.json.new" "$WEB_ROOT/stable/manifest.json"
echo "Published Millennium OS $RELEASE_ID"
