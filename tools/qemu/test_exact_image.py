import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).with_name("exact_image_test.py")
SPEC = importlib.util.spec_from_file_location("exact_image_test", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class ExactImageHarnessTests(unittest.TestCase):
    def test_marker_must_be_a_standalone_console_line(self):
        marker = MODULE.PASS_MARKER
        echoed = "systemd: ExecStart=/bin/sh -c 'printf " + marker + "'"

        self.assertFalse(MODULE.has_marker(echoed, marker))
        self.assertTrue(MODULE.has_marker("before\n" + marker + "\nafter\n", marker))

    def test_each_system_slot_can_be_selected(self):
        self.assertIn(b'KERNEL=="vda5"', MODULE.shell_commands(5))
        self.assertIn(b'KERNEL=="vda6"', MODULE.shell_commands(6))

    def test_qemu_entrypoint_requires_the_dual_slot_matrix(self):
        entrypoint = MODULE_PATH.with_name("qemu.sh").read_text()
        matrix = MODULE_PATH.with_name("exact_image_matrix.py").read_text()

        self.assertIn('python3 "$SCRIPT_DIR/exact_image_matrix.py"', entrypoint)
        self.assertIn("for partition in (5, 6):", matrix)

    def test_default_timeout_allows_slow_external_image_boot(self):
        self.assertEqual(MODULE.DEFAULT_TIMEOUT_SECONDS, 600)

    def test_injects_all_production_slot_aliases_and_acceptance_unit(self):
        commands = MODULE.shell_commands().decode()

        self.assertIn("/etc/udev/rules.d/99-qemu-slot.rules", commands)
        self.assertIn("/etc/systemd/system/qemu-exact-accept.service", commands)
        self.assertIn("ExecStart=/bin/sh -xc", commands)
        self.assertIn("exec >/dev/console 2>&1", commands)
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
            "ExecMainStatus --value daemon.service",
            "millennium-wired.nmconnection",
            "grep -Fxc never-default=true",
            r"grep -Fqx no-auto-default=\*",
            "/etc/NetworkManager/conf.d/10-millennium.conf",
            "nmcli -g ipv4.never-default connection show millennium-wired",
            "nmcli -g ipv6.never-default connection show millennium-wired",
            "grep -Fqx Restart=no",
            "grep -Fqx RuntimeMaxSec=900",
            "grep -Fqx BindsTo=millennium-wifi-helper.service",
            "stat -c %%U:%%G:%%a /run/millennium-wifi",
            "root:millennium-wifi:750",
            "runuser -u millennium-wifi -- test -x /run/millennium-wifi",
            "systemctl show -p Group --value",
        ):
            self.assertIn(expected, commands)
        self.assertNotIn("systemctl is-active --quiet daemon.service", commands)
        self.assertNotIn(MODULE.PASS_MARKER, commands)
        self.assertNotIn(MODULE.FAIL_MARKER, commands)
        self.assertIn(r"\120\101\123\123", commands)
        self.assertIn(r"\106\101\111\114", commands)

    def test_result_records_the_exact_image_size(self):
        source = MODULE_PATH.read_text(encoding="utf-8")
        self.assertIn('"image_size": args.image.stat().st_size', source)

    def test_generic_guest_installs_and_exercises_real_wifi_identities(self):
        provision = MODULE_PATH.with_name("provision-guest.sh").read_text(
            encoding="utf-8")
        guest_test = MODULE_PATH.with_name("wifi-test-guest.sh").read_text(
            encoding="utf-8")
        for expected in (
            "systemd-sysusers /usr/lib/sysusers.d/millennium-wifi.conf",
            "systemd/millennium-wifi-bootstrap.service",
            "wifi/millennium_wifi_helper.py",
            "/etc/NetworkManager/system-connections",
            "/var/lib/millennium/wifi",
        ):
            self.assertIn(expected, provision)
        for expected in (
            "systemctl start millennium-wifi-bootstrap.service",
            "systemctl start millennium-wifi-helper.service",
            "runuser -u millennium-wifi -- test -x /run/millennium-wifi",
            'connection.connect("/run/millennium-wifi/helper.sock")',
        ):
            self.assertIn(expected, guest_test)

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
