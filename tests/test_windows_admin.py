"""On real Windows (CI): Windows' own "Access is denied" is what PR7-E sets aside, and the administrator start
(ShellExecuteEx "runas" -> PR7-E --elevated-ops) really copies. CI's runner is an administrator with no prompt."""
import os
import subprocess
import sys

import pytest

from pr7e import elevated, ops

pytestmark = pytest.mark.skipif(sys.platform != "win32" or not os.environ.get("CI"), reason="Windows CI only")


def _bundle(root):
    b = root / "Paragon PR7-T.vst3"
    (b / "Contents" / "x86_64-win").mkdir(parents=True)
    (b / "Contents" / "x86_64-win" / "Paragon PR7-T.vst3").write_bytes(b"MZ" * 5000)
    (b / "Contents" / "moduleinfo.json").write_text("{}")
    return b


def test_real_access_denied_is_set_aside(tmp_path):
    locked = tmp_path / "Locked"
    locked.mkdir()
    user = os.environ.get("USERNAME", "")
    subprocess.run(["icacls", str(locked), "/deny", f"{user}:(OI)(CI)(W,AD,WD)"], check=True, capture_output=True)
    try:
        job = ops.CopyJob([str(_bundle(tmp_path / "src"))], str(locked))
        asked = []
        job.ask_admin = lambda what: asked.append(what) or False
        job.run()
        assert asked == [(1, str(locked))] and job.errors and "Access is denied" in job.errors[0]
    finally:
        subprocess.run(["icacls", str(locked), "/remove:d", user], capture_output=True)


def test_the_administrator_copy_really_runs(tmp_path):
    src = _bundle(tmp_path / "src")
    target = tmp_path / "VST3" / "Paragon PR7-T.vst3"
    done = elevated.run_elevated([{"what": "copy", "src": str(src), "target": str(target), "replace": False,
                                   "merge": False}], timeout=120)
    assert done == [None]
    assert (target / "Contents" / "moduleinfo.json").exists()
