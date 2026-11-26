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


def test_the_exchange_folder_lets_this_user_read_what_the_administrator_wrote():
    """mkdtemp's folder (mode 0o700) allows only SYSTEM, Administrators and OWNER RIGHTS, and the elevated PR7-E's
    results file is owned by Administrators: an unelevated PR7-E then can't read it (the user's "Administrator
    permission wasn't given" after the move was done). The exchange folder inherits Temp's access: the user by name."""
    import shutil
    import tempfile
    user = os.environ["USERNAME"].lower()

    def acl(path):
        return subprocess.run(["icacls", path], capture_output=True, text=True, encoding="oem").stdout.lower()
    private = tempfile.mkdtemp()
    shared = elevated.exchange_folder()
    try:
        assert "owner rights" in acl(private) and user not in acl(private)    # the cause, as Windows shows it
        assert user in acl(shared) and "owner rights" not in acl(shared)
    finally:
        shutil.rmtree(private, ignore_errors=True)
        shutil.rmtree(shared, ignore_errors=True)


def test_a_folder_windows_wont_let_this_account_delete(tmp_path):
    """The Recycle Bin call is never made for an item this account may not delete (it would show Windows' own
    administrator box, which can't work from PR7-E): `may_delete` says no, and send raises "access denied", which
    TrashJob turns into PR7-E's one question. Delete rights are denied on the item and on its folder (Windows also
    lets a folder's "delete child" right delete what's in it)."""
    from pr7e import trash
    user = os.environ["USERNAME"]
    parent = tmp_path / "Protected"
    item = parent / "Behemoth Bass.vst3"
    (item / "Contents").mkdir(parents=True)
    subprocess.run(["icacls", str(item), "/deny", f"{user}:(DE)"], check=True, capture_output=True)
    subprocess.run(["icacls", str(parent), "/deny", f"{user}:(DC)"], check=True, capture_output=True)
    try:
        assert trash.may_delete(str(item)) is False
        with pytest.raises(trash.TrashAccessDenied) as caught:
            trash.WindowsTrash().send(str(item))
        assert elevated.needs_admin(caught.value)
        asked = []
        job = ops.TrashJob([str(item)])
        job.ask_admin = lambda what: asked.append(what) or False
        job.run()
        assert asked == [(1, str(parent), "delete")] and item.exists() and job.untrashable == []
        assert trash.may_delete(str(tmp_path)) is True
    finally:
        subprocess.run(["icacls", str(parent), "/remove:d", user], capture_output=True)
        subprocess.run(["icacls", str(item), "/remove:d", user], capture_output=True)
