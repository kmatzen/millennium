from pathlib import Path
import os
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "host/monitoring/millennium_backup_pull.sh"


class BackupPullTests(unittest.TestCase):
    def run_backup(self, fail_export: bool):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            bindir = root / "bin"
            config = root / "config"
            fixture = root / "fixture"
            for directory in (bindir, config, fixture):
                directory.mkdir()
            (config / "restic-password").write_text("test\n")
            (config / "pull-key").write_text("test\n")
            (config / "phone-known-hosts").write_text("test\n")
            (fixture / "state.txt").write_text("phone state\n")

            self.executable(bindir / "ssh", r"""
                #!/bin/sh
                last=
                for value in "$@"; do last=$value; done
                if [ "$last" = backup ]; then
                    if [ "${TEST_FAIL_EXPORT:-0}" = 1 ]; then exit 23; fi
                    exec /usr/bin/tar -C "$TEST_FIXTURE" -cf - state.txt
                fi
                if [ "$last" = ack ]; then
                    echo ack >> "$TEST_LOG"
                    exit 0
                fi
                exit 2
            """)
            self.executable(bindir / "restic", r"""
                #!/bin/sh
                command=
                for value in "$@"; do
                    case "$value" in backup|snapshots|dump|forget) command=$value; break;; esac
                done
                case "$command" in
                    backup)
                        /bin/cat > "$TEST_ARCHIVE"
                        echo backup >> "$TEST_LOG"
                        ;;
                    snapshots)
                        printf '[{"id":"test-snapshot-id"}]\n'
                        ;;
                    dump)
                        /bin/cat "$TEST_ARCHIVE"
                        ;;
                    forget)
                        echo "forget $*" >> "$TEST_LOG"
                        ;;
                    *) exit 2;;
                esac
            """)
            server = bindir / "server-backup"
            self.executable(server, """
                #!/bin/sh
                echo server >> "$TEST_LOG"
            """)

            environment = os.environ.copy()
            environment.update({
                "PATH": f"{bindir}:{environment['PATH']}",
                "MILLENNIUM_BACKUP_CONFIG_ROOT": str(config),
                "MILLENNIUM_BACKUP_REPOSITORY": str(root / "repository"),
                "MILLENNIUM_SERVER_BACKUP_COMMAND": str(server),
                "RESTIC_PASSWORD_FILE": str(config / "restic-password"),
                "TEST_ARCHIVE": str(root / "archive.tar"),
                "TEST_FAIL_EXPORT": "1" if fail_export else "0",
                "TEST_FIXTURE": str(fixture),
                "TEST_LOG": str(root / "calls.log"),
            })
            result = subprocess.run(
                ["bash", str(SCRIPT)], text=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, env=environment)
            return result, (root / "calls.log").read_text().splitlines()

    @staticmethod
    def executable(path: Path, content: str) -> None:
        path.write_text(textwrap.dedent(content).lstrip())
        path.chmod(0o755)

    def test_failed_export_removes_run_snapshot_but_runs_server_backup(self):
        result, calls = self.run_backup(True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("backup", calls)
        self.assertTrue(any(line.startswith("forget ") and
                            "test-snapshot-id" in line for line in calls))
        self.assertIn("server", calls)
        self.assertNotIn("ack", calls)

    def test_success_is_restore_checked_before_ack(self):
        result, calls = self.run_backup(False)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("server", calls)
        self.assertIn("ack", calls)
        self.assertTrue(any(line.startswith("forget ") and
                            "--keep-daily" in line for line in calls))
