import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).with_name("exact_image_test.py")
SPEC = importlib.util.spec_from_file_location("exact_image_test", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class ExactImageHarnessTests(unittest.TestCase):
    def test_injects_all_production_slot_aliases_and_acceptance_unit(self):
        commands = MODULE.shell_commands().decode()

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
        ):
            self.assertIn(expected, commands)
        self.assertNotIn(MODULE.PASS_MARKER, commands)
        self.assertNotIn(MODULE.FAIL_MARKER, commands)
        self.assertIn(r"\120\101\123\123", commands)
        self.assertIn(r"\106\101\111\114", commands)

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
