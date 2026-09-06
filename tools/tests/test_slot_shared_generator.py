import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "host/os_image/slot-shared-generator"


class SlotSharedGeneratorTests(unittest.TestCase):
    def test_generated_mounts_are_required_by_local_fs_target(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            conf = root / "conf"
            output = root / "generator"
            tools = root / "bin"
            conf.mkdir()
            tools.mkdir()
            (conf / "millennium.conf").write_text(
                "Version=1\nPath=/etc/millennium\n"
                "Path=/etc/NetworkManager/system-connections\n",
                encoding="utf-8")
            escape = tools / "systemd-escape"
            escape.write_text(
                "#!/bin/sh\nprintf '%s' \"$2\" | sed "
                "'s|^/||; s|-|\\\\x2d|g; s|/|-|g'\n",
                encoding="utf-8")
            escape.chmod(0o755)
            environment = dict(os.environ)
            environment["SLOT_SHARED_CONF_DIR"] = str(conf)
            environment["PATH"] = str(tools) + os.pathsep + environment["PATH"]
            subprocess.run([str(GENERATOR), str(output)], check=True, env=environment)

            names = ("etc-millennium.mount",
                     r"etc-NetworkManager-system\x2dconnections.mount")
            for name in names:
                unit = output / name
                dependency = output / "local-fs.target.requires" / name
                self.assertTrue(unit.is_file())
                self.assertTrue(dependency.is_symlink())
                self.assertEqual(dependency.readlink(), Path("../" + name))


if __name__ == "__main__":
    unittest.main()
