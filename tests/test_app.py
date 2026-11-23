import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_version():
    from pr7e import __version__
    out = subprocess.run([sys.executable, "-m", "pr7e", "--version"], cwd=ROOT, capture_output=True, text=True)
    assert out.stdout.strip() == __version__


def test_selftest_lists_a_folder(tmp_path):
    (tmp_path / "folder").mkdir()
    (tmp_path / "folder" / "b.txt").write_text("b")
    (tmp_path / "folder" / "A").mkdir()
    out = tmp_path / "out.json"
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
    r = subprocess.run([sys.executable, "-m", "pr7e", "--selftest", str(tmp_path / "folder"), str(out)], cwd=ROOT,
                       env=env, capture_output=True, text=True, timeout=120)
    result = json.loads(out.read_text())
    assert r.returncode == 0, result
    assert result["names"] == ["A", "b.txt"] and result["fonts"] is True


def test_shell_command_line():
    from pr7e import shell
    assert shell.command("%V").endswith('--explorer "%V"')
    assert set(shell.KEYS) == {r"Software\Classes\Directory\shell\PR7E", r"Software\Classes\Drive\shell\PR7E",
                               r"Software\Classes\Directory\Background\shell\PR7E"}
