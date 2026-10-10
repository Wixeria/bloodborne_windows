"""Check GUI runner argv, separate native logs, restart and the final exit code."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def main():
    fixture = Path(sys.argv[1]).resolve()
    runner = Path(__file__).resolve().parents[1] / "run-windows.ps1"
    with tempfile.TemporaryDirectory(prefix="bb-launcher-runner-") as temporary:
        root = Path(temporary) / "O'Brien & Unicode 游戏"
        root.mkdir()
        shutil.copyfile(fixture, root / "bb-probe.exe")
        boot = root / "boot image.bin"
        boot.write_bytes(b"fixture")
        prefix = root / "session log"
        environment = {k: v for k, v in os.environ.items() if not k.upper().startswith("BB_")}
        environment["BB_RUNNER_TEST_STATE"] = str(root / "attempt.txt")
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(runner),
             "-BootImage", str(boot), "-BuildDirectory", str(root), "-SkipBuild",
             "-GameLogPrefix", str(prefix), "-Timeout", "0"],
            env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15,
            creationflags=subprocess.CREATE_NO_WINDOW)
        assert result.returncode == 23, (result.returncode, result.stdout)
        assert b"Overlay requested a restart" in result.stdout, result.stdout
        expected = [str(boot), "--timeout", "0", "--cpu-only"]
        for attempt in (1, 2):
            lines = Path(f"{prefix}-{attempt}-stdout.log").read_text().splitlines()
            assert f"attempt={attempt}" in lines, lines
            arguments = [bytes.fromhex(line[4:]).decode("utf-8") for line in lines if line.startswith("arg=")]
            assert arguments == expected, arguments
            errors = Path(f"{prefix}-{attempt}-stderr.log").read_text()
            assert f"native stderr attempt {attempt}" in errors, errors
        assert (root / "attempt.txt").read_text() == "2"
    print("GUI runner passed: literal Unicode paths, both streams, restart 75 and final exit 23.")


if __name__ == "__main__":
    main()
