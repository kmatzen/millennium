from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
LAYER = ROOT / "host/os_image/layer/millennium-phone-image.yaml"
WIFI_BOOTSTRAP_UNIT = ROOT / "host/systemd/millennium-wifi-bootstrap.service"
WIFI_HELPER_UNIT = ROOT / "host/systemd/millennium-wifi-helper.service"
WIFI_PORTAL_UNIT = ROOT / "host/systemd/millennium-wifi-portal.service"


class Zero2WImageLayerTests(unittest.TestCase):
    def test_physical_wifi_backend_and_monitor_output_are_installed(self) -> None:
        text = LAYER.read_text()
        unit = WIFI_BOOTSTRAP_UNIT.read_text()

        self.assertIn("    - wpasupplicant\n", text)
        self.assertIn("    - firmware-realtek\n", text)
        self.assertIn("      no-auto-default=*\n", text)
        self.assertIn("id=millennium-wired", text)
        self.assertIn("interface-name=eth0", text)
        self.assertEqual(text.count("never-default=true"), 2)
        self.assertIn(
            'install -d -m 0755 '
            '"$1/var/lib/node_exporter/textfile_collector"',
            text,
        )
        self.assertIn("Wants=NetworkManager.service wpa_supplicant.service", unit)
        self.assertIn("After=NetworkManager.service wpa_supplicant.service", unit)
        self.assertIn("Group=millennium-wifi", unit)
        self.assertIn("RuntimeDirectoryPreserve=yes", unit)
        self.assertIn(
            'chroot "$1" systemd-sysusers '
            "/usr/lib/sysusers.d/millennium-wifi.conf",
            text,
        )
        self.assertIn('chroot "$1" getent passwd millennium-wifi', text)
        self.assertIn('chroot "$1" getent group millennium-wifi', text)
        tmpfiles = (ROOT / "host/systemd/millennium-monitor.tmpfiles").read_text()
        staging = (ROOT / "tools/stage_zero2w_image_payload.sh").read_text()
        self.assertIn(
            "/var/lib/node_exporter/textfile_collector 0755 millennium millennium",
            tmpfiles,
        )
        self.assertIn("millennium-monitor.tmpfiles", staging)
        self.assertIn("millennium-monitor.conf", staging)

    def test_persistent_state_parent_preserves_content_immutability(self) -> None:
        text = LAYER.read_text()
        tmpfiles = (ROOT / "host/systemd/millennium-experience.tmpfiles").read_text()

        self.assertNotIn(
            "chown -R millennium:millennium /var/lib/millennium ", text
        )
        self.assertIn('chroot "$1" chown 0:0 /var/lib/millennium', text)
        self.assertIn('chroot "$1" chown -R 0:0 /var/lib/millennium/content', text)
        self.assertIn("d /var/lib/millennium 0755 root root -", tmpfiles)
        self.assertIn(
            "/var/lib/millennium/content/owner-requests", text
        )
        self.assertIn("/var/lib/millennium/story-state", text)
        self.assertIn("/var/lib/millennium/state", text)

    def test_wifi_state_owner_is_resolved_inside_target_root(self) -> None:
        text = LAYER.read_text()

        create = 'install -d -m 0700 "$1/var/lib/millennium/wifi"'
        target_chown = (
            'chroot "$1" chown -R millennium:millennium \\\n'
            '        /var/lib/millennium/wifi /var/log/millennium'
        )

        self.assertNotIn(
            'install -d -o millennium -g millennium -m 0700 '
            '"$1/var/lib/millennium/wifi"',
            text,
        )
        self.assertIn(create, text)
        self.assertIn(target_chown, text)
        self.assertLess(text.index(create), text.index(target_chown))

    def test_wifi_dependents_check_setup_marker_after_bootstrap(self) -> None:
        for path in (WIFI_HELPER_UNIT, WIFI_PORTAL_UNIT):
            unit = path.read_text()
            self.assertNotIn("ConditionPathExists=/run/millennium-wifi/setup-active", unit)
            self.assertIn(
                "ExecCondition=/usr/bin/test -e /run/millennium-wifi/setup-active",
                unit,
            )
        helper = WIFI_HELPER_UNIT.read_text()
        portal = WIFI_PORTAL_UNIT.read_text()
        bootstrap = WIFI_BOOTSTRAP_UNIT.read_text()
        self.assertIn("Group=millennium-wifi", bootstrap)
        self.assertIn("RuntimeMaxSec=900", helper)
        self.assertIn("Restart=no", helper)
        self.assertNotIn("Restart=on-failure", helper)
        self.assertIn("BindsTo=millennium-wifi-helper.service", portal)
        self.assertIn("PartOf=millennium-wifi-helper.service", portal)

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
            'find "$1/etc" "$1/opt" "$1/usr" -xdev -type d -uid 1000',
            cleanup_text,
        )
        self.assertIn('-exec chown 0:0 {} + -exec chmod o+rx {} +', cleanup_text)

    def test_production_release_is_traversable_by_service_account(self) -> None:
        text = LAYER.read_text()
        cleanup_text = text[text.index("  cleanup-hooks:"):]

        self.assertIn('chown 0:0 "$1/opt"', cleanup_text)
        self.assertIn('chmod 0755 "$1/opt"', cleanup_text)
        self.assertIn('"$1/opt"', cleanup_text)
        self.assertIn('chmod o+rx', cleanup_text)
