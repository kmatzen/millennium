from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


class AdminTokenPermissionContractTests(unittest.TestCase):
    def test_daemon_accepts_only_its_own_group_read_access(self):
        source = (ROOT / "host/daemon.c").read_text()
        body = re.search(
            r"static int load_admin_token\(.*?\n}\n",
            source,
            flags=re.DOTALL,
        )
        self.assertIsNotNone(body)
        check = body.group(0)
        self.assertIn("S_ISREG(st.st_mode)", check)
        self.assertIn("st.st_gid != getegid()", check)
        self.assertIn("S_IROTH", check)
        self.assertIn("S_IWGRP", check)
        self.assertIn("S_IXGRP", check)
        self.assertIn("S_IRUSR", check)

    def test_factory_token_mode_matches_runtime_contract(self):
        source = (ROOT / "tools/factory_seed_zero2w.py").read_text()
        self.assertIn('0o640 if item.name == "admin-token"', source)
        self.assertIn("copy_file(item, destination / item.name, mode,\n"
                      "                  0, 1000)", source)


if __name__ == "__main__":
    unittest.main()
