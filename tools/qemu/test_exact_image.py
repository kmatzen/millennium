import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).with_name("exact_image_test.py")
SPEC = importlib.util.spec_from_file_location("exact_image_test", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class ExactImageHarnessTests(unittest.TestCase):
    def test_default_timeout_allows_slow_external_image_boot(self):
        self.assertEqual(MODULE.DEFAULT_TIMEOUT_SECONDS, 600)

    def test_injects_all_production_slot_aliases_and_acceptance_unit(self):
        commands = MODULE.shell_commands().decode()

        self.assertIn("/etc/udev/rules.d/99-qemu-slot.rules", commands)
        self.assertIn("/etc/systemd/system/qemu-exact-accept.service", commands)
        self.assertNotIn("/run/udev/rules.d", commands)

        for expected in (
            'vda1", SYMLINK+="disk/by-slot/bootconfig',
            'vda2", SYMLINK+="disk/by-slot/active/boot',
            'vda5", SYMLINK+="disk/by-slot/active/system',
            'vda7", SYMLINK+="disk/by-slot/persistent',
            "boot-firmware.mount bootfs.mount nftables.service",
            "millennium-firewall.service",
            "systemctl is-active --quiet boot-firmware.mount",
            "systemctl is-active --quiet bootfs.mount",
            "systemctl is-active --quiet nftables.service",
            "runuser -u messagebus -- test -x /usr/bin/dbus-daemon",
            "runuser -u systemd-resolve -- test -r /etc/systemd/resolved.conf",
            "systemctl is-active --quiet NetworkManager.service",
            "millennium-wired.nmconnection",
            "grep -Fxc never-default=true",
            r"grep -Fqx no-auto-default=\*",
            "nmcli -g ipv4.never-default connection show millennium-wired",
            "nmcli -g ipv6.never-default connection show millennium-wired",
            "grep -Fqx Restart=no",
            "grep -Fqx RuntimeMaxSec=900",
            "grep -Fqx BindsTo=millennium-wifi-helper.service",
            "stat -c %U:%G:%a /run/millennium-wifi",
            "root:millennium-wifi:750",
            "runuser -u millennium-wifi -- test -x /run/millennium-wifi",
            "systemctl show -p Group --value",
        ):
            self.assertIn(expected, commands)
        self.assertNotIn(MODULE.PASS_MARKER, commands)
        self.assertNotIn(MODULE.FAIL_MARKER, commands)
        self.assertIn(r"\120\101\123\123", commands)
        self.assertIn(r"\106\101\111\114", commands)

    def test_result_records_the_exact_image_size(self):
        source = MODULE_PATH.read_text(encoding="utf-8")
        self.assertIn('"image_size": args.image.stat().st_size', source)

    def test_forbidden_failures_cover_the_physical_regressions(self):
        forbidden = "\n".join(MODULE.FORBIDDEN)
        self.assertIn("Failed to start dbus.service", forbidden)
        self.assertIn("resolved.conf: Permission denied", forbidden)
        self.assertIn("Failed to mount bootfs.mount", forbidden)
        self.assertIn("Failed to start millennium-firewall.service", forbidden)

    def test_console_normalization_preserves_failure_text(self):
        console = (
            b"[\x1b[0;1;31mFAILED\x1b[0m] Failed to start "
            b"\x1b[0;1;39msystemd-resolved.service\x1b[0m\r\n"
        )
        self.assertIn(
            "Failed to start systemd-resolved.service",
            MODULE.normalized_console(console),
        )


if __name__ == "__main__":
    unittest.main()
