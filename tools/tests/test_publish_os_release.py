import gzip
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
PUBLISH = ROOT / "tools/publish_os_release.sh"


class PublishOsReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.release = self.root / "release"
        self.web = self.root / "web"
        self.release.mkdir()
        self.private = self.root / "private.pem"
        self.public = self.root / "public.pem"
        subprocess.run(["openssl", "genpkey", "-algorithm", "ED25519",
                        "-out", str(self.private)], check=True,
                       stdout=subprocess.DEVNULL)
        subprocess.run(["openssl", "pkey", "-in", str(self.private),
                        "-pubout", "-out", str(self.public)], check=True,
                       stdout=subprocess.DEVNULL)
        images = {}
        for role, data in (("boot", b"boot image"), ("root", b"root image")):
            name = f"{role}-00000001-1.0.0.img.gz"
            path = self.release / name
            with path.open("wb") as raw:
                with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0) as stream:
                    stream.write(data)
            compressed = path.read_bytes()
            images[role] = {
                "url": "https://updates.example/releases/00000001-1.0.0/" + name,
                "compression": "gzip", "compressed_size": len(compressed),
                "compressed_sha256": hashlib.sha256(compressed).hexdigest(),
                "expanded_size": len(data),
                "expanded_sha256": hashlib.sha256(data).hexdigest(),
            }
        manifest = {"schema": 1, "kind": "millennium-os-release",
                    "sequence": 1, "version": "1.0.0", "images": images}
        self.manifest = self.release / "manifest.json"
        self.manifest.write_text(json.dumps(manifest, sort_keys=True) + "\n")
        subprocess.run(["openssl", "pkeyutl", "-sign", "-rawin", "-inkey",
                        str(self.private), "-in", str(self.manifest), "-out",
                        str(self.release / "manifest.json.sig")], check=True)

    def tearDown(self):
        self.temporary.cleanup()

    def publish(self):
        return subprocess.run([str(PUBLISH), str(self.release), str(self.web),
                               str(self.public)], text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def test_publishes_verified_immutable_release_and_stable_pointer(self):
        result = self.publish()
        self.assertEqual(result.returncode, 0, result.stderr)
        destination = self.web / "releases/00000001-1.0.0"
        self.assertEqual(len(list(destination.glob("*.img.gz"))), 2)
        self.assertEqual((self.web / "stable/manifest.json").read_bytes(),
                         self.manifest.read_bytes())
        second = self.publish()
        self.assertNotEqual(second.returncode, 0)
        self.assertIn("release already exists", second.stderr)

    def test_rejects_payload_not_matching_signed_manifest(self):
        with (self.release / "boot-00000001-1.0.0.img.gz").open("ab") as stream:
            stream.write(b"tamper")
        result = self.publish()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match signed manifest", result.stderr)
        self.assertFalse(self.web.exists())

    def test_rejects_manifest_with_invalid_signature(self):
        with self.manifest.open("ab") as stream:
            stream.write(b" ")
        result = self.publish()
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.web.exists())


if __name__ == "__main__":
    unittest.main()
