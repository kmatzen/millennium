from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
LAYER = ROOT / "host/os_image/layer/millennium-phone-image.yaml"


class Zero2WImageLayerTests(unittest.TestCase):
    def test_wifi_state_owner_is_resolved_inside_target_root(self) -> None:
        text = LAYER.read_text()

        create = 'install -d -m 0700 "$1/var/lib/millennium/wifi"'
        target_chown = (
            'chroot "$1" chown -R millennium:millennium '
            "/var/lib/millennium /var/log/millennium"
        )

        self.assertNotIn('install -d -o millennium -g millennium', text)
        self.assertIn(create, text)
        self.assertIn(target_chown, text)
        self.assertLess(text.index(create), text.index(target_chown))

    def test_fixed_shared_generator_is_installed_after_upstream_customize(
            self) -> None:
        text = LAYER.read_text()

        cleanup = "  cleanup-hooks:"
        source = '"$SRCROOT/slot-shared-generator"'
        destination = (
            '"$1/usr/lib/systemd/system-generators/slot-shared-generator"'
        )

        self.assertIn(cleanup, text)
        self.assertGreater(text.index(cleanup), text.index("  customize-hooks:"))
        cleanup_text = text[text.index(cleanup):]
        self.assertIn("install -D -m 0755", cleanup_text)
        self.assertIn(source, cleanup_text)
        self.assertIn(destination, cleanup_text)
        self.assertIn("cmp ", cleanup_text)

    def test_payload_merge_preserves_base_directory_metadata(self) -> None:
        text = LAYER.read_text()

        self.assertNotIn('cp -a "$payload/." "$1/"', text)
        self.assertIn('--no-same-owner --no-overwrite-dir', text)

    def test_upstream_overlay_directory_metadata_is_normalized_last(self) -> None:
        text = LAYER.read_text()
        cleanup_text = text[text.index("  cleanup-hooks:"):]

        self.assertIn(
            'find "$1/etc" "$1/usr" -xdev -type d -uid 1000',
            cleanup_text,
        )
        self.assertIn('-exec chown 0:0 {} + -exec chmod o+rx {} +', cleanup_text)
