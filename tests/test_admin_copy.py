"""Copying into a folder only an administrator may change (Program Files' VST3 folder): what Windows refuses is set
aside, PR7-E asks once, and the copy is finished with administrator rights, as Explorer does it.

Tests run as root on Linux, where nothing is refused: `protected` makes one folder answer "Access is denied" to every
write until `admin` is on, and the stand-in elevation turns it on around the real elevated.perform."""
import builtins
import errno
import os
import shutil

import pytest

from pr7e import elevated, ops


@pytest.fixture
def protected(tmp_path, monkeypatch):
    """A folder like C:\\Program Files\\Common Files\\VST3: readable, every write refused unless `admin`."""
    folder = tmp_path / "Program Files" / "Common Files" / "VST3"
    folder.mkdir(parents=True)
    state = {"admin": False, "elevated": 0}
    root = os.path.normcase(str(folder))

    def inside(p) -> bool:
        try:
            return os.path.normcase(os.path.abspath(os.fspath(p))).startswith(root)
        except TypeError:
            return False

    def guard(fn, writes=lambda *a, **k: True):
        def wrapped(path, *a, **k):
            if inside(path) and not state["admin"] and writes(path, *a, **k):
                raise PermissionError(errno.EACCES, "Access is denied", os.fspath(path))
            return fn(path, *a, **k)
        return wrapped
    real_open, real_rename = builtins.open, os.rename
    monkeypatch.setattr(builtins, "open", guard(real_open, lambda p, mode="r", *a, **k: any(c in mode for c in "wax+")))
    for name in ("makedirs", "mkdir", "remove", "unlink", "rmdir"):
        monkeypatch.setattr(os, name, guard(getattr(os, name)))

    def rename(src, dst, *a, **k):
        if (inside(dst) or inside(src)) and not state["admin"]:
            raise PermissionError(errno.EACCES, "Access is denied", os.fspath(dst))
        return real_rename(src, dst, *a, **k)
    monkeypatch.setattr(os, "rename", rename)

    def elevate(op_list):
        state["elevated"] += 1
        state["admin"] = True
        try:
            return elevated.perform(op_list)
        finally:
            state["admin"] = False
    return folder, state, elevate


def plugin(tmp_path, version="1.0"):
    """A VST3 bundle like Paragon PR7-T.vst3 (a folder with the binary and moduleinfo.json inside)."""
    b = tmp_path / "build" / "Paragon PR7-T.vst3"
    (b / "Contents" / "x86_64-win").mkdir(parents=True)
    (b / "Contents" / "x86_64-win" / "Paragon PR7-T.vst3").write_bytes(b"MZ" + version.encode() * 1000)
    (b / "Contents" / "moduleinfo.json").write_text('{"Version": "%s"}' % version)
    return b


def test_refused_copy_asks_once_and_finishes_as_administrator(tmp_path, protected):
    folder, state, elevate = protected
    src = plugin(tmp_path)
    asked = []
    job = ops.CopyJob([str(src)], str(folder))
    job.ask_admin = lambda what: asked.append(what) or True
    job.elevate = elevate
    job.run()
    assert job.state == "done" and job.errors == []
    assert asked == [(1, str(folder))] and state["elevated"] == 1          # one question, one elevation
    dest = folder / "Paragon PR7-T.vst3"
    assert (dest / "Contents" / "moduleinfo.json").read_text() == '{"Version": "1.0"}'
    assert (dest / "Contents" / "x86_64-win" / "Paragon PR7-T.vst3").read_bytes() == \
        (src / "Contents" / "x86_64-win" / "Paragon PR7-T.vst3").read_bytes()
    assert job.created == [str(dest)] and job.fraction() == 1.0


def test_replacing_an_installed_plugin(tmp_path, protected):
    """The case in the screenshot: the plugin is there already; Replace (or Merge) needs administrator rights too."""
    folder, state, elevate = protected
    state["admin"] = True
    shutil.copytree(plugin(tmp_path / "old", "0.9"), folder / "Paragon PR7-T.vst3")
    state["admin"] = False
    src = plugin(tmp_path, "1.0")
    for answer in ("merge", "replace"):
        job = ops.CopyJob([str(src)], str(folder))
        job.ask = lambda c, a=answer: (a, True)
        job.ask_admin = lambda what: True
        job.elevate = elevate
        job.run()
        assert job.errors == [], answer
        assert (folder / "Paragon PR7-T.vst3" / "Contents" / "moduleinfo.json").read_text() == '{"Version": "1.0"}'


def test_said_no_or_windows_refused(tmp_path, protected):
    folder, state, elevate = protected
    src = plugin(tmp_path)
    job = ops.CopyJob([str(src)], str(folder))
    job.ask_admin = lambda what: False                                     # Cancel on PR7-E's question
    job.elevate = elevate
    job.run()
    assert state["elevated"] == 0 and job.errors == ["\u201cParagon PR7-T.vst3\u201d: Access is denied"]
    assert not (folder / "Paragon PR7-T.vst3").exists()
    job = ops.CopyJob([str(src)], str(folder))
    job.ask_admin = lambda what: True
    job.elevate = lambda op_list: None                                     # No on Windows' own prompt
    job.run()
    assert job.errors[-1] == "Administrator permission wasn't given, so these weren't copied."


def test_a_move_never_deletes_before_the_copy_is_done(tmp_path, protected, monkeypatch):
    folder, state, elevate = protected
    src = plugin(tmp_path)
    monkeypatch.setattr(ops.fs, "same_volume", lambda a, b: False)        # another drive: copy, then delete
    job = ops.CopyJob([str(src)], str(folder), move=True)
    answers = []
    job.ask_admin = lambda what: answers.append(what) or False
    job.elevate = elevate
    job.run()
    assert (src / "Contents" / "moduleinfo.json").exists()                # said no: the original is still there
    job = ops.CopyJob([str(src)], str(folder), move=True)
    job.ask_admin = lambda what: True
    job.elevate = elevate
    job.run()
    assert job.errors == [] and not src.exists()
    assert (folder / "Paragon PR7-T.vst3" / "Contents" / "moduleinfo.json").exists()
    assert job.undo is not None                                            # undo knows where it went


def test_a_file_in_use_isnt_an_administrator_question(tmp_path, monkeypatch):
    """Windows' "being used by another process" (32): administrator rights wouldn't help, so it's just an error."""
    e = PermissionError(13, "The process cannot access the file because it is being used by another process")
    e.winerror = 32
    assert not elevated.needs_admin(e)
    d = PermissionError(13, "Access is denied")
    d.winerror = 5
    assert elevated.needs_admin(d) and not elevated.needs_admin(FileNotFoundError(2, "x"))


def test_elevated_ops_command(tmp_path):
    """`PR7-E.exe --elevated-ops IN OUT`, what Windows starts with administrator rights."""
    import json
    import subprocess
    import sys
    src = plugin(tmp_path)
    ops_file, out = tmp_path / "ops.json", tmp_path / "done.json"
    target = tmp_path / "dest" / "Paragon PR7-T.vst3"
    ops_file.write_text(json.dumps([{"what": "copy", "src": str(src), "target": str(target), "replace": False,
                                     "merge": False}, {"what": "copy", "src": str(tmp_path / "nope"),
                                                       "target": str(tmp_path / "x")}]))
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    r = subprocess.run([sys.executable, "-m", "pr7e", "--elevated-ops", str(ops_file), str(out)], cwd=root,
                       capture_output=True, text=True, timeout=60)
    got = json.loads(out.read_text())
    assert r.returncode == 0 and got[0] is None and got[1]
    assert (target / "Contents" / "moduleinfo.json").exists()


def test_the_window_asks_explorers_question(qapp, tmp_path, protected, clean_settings):
    from pr7e.main_window import MainWindow
    folder, state, elevate = protected
    src = plugin(tmp_path)
    w = MainWindow(str(tmp_path / "build"))
    w.sync_jobs = True
    asked = []
    w.answers["admin"] = lambda what: asked.append(what) or True
    real = ops.CopyJob.__init__

    def init(self, *a, **k):
        real(self, *a, **k)
        self.elevate = elevate
    import unittest.mock
    with unittest.mock.patch.object(ops.CopyJob, "__init__", init):
        w.runner.start(ops.CopyJob([str(src)], str(folder)), sync=True)
    assert asked == [(1, str(folder))] and (folder / "Paragon PR7-T.vst3" / "Contents" / "moduleinfo.json").exists()
    w.close()


def test_what_the_disk_shows_when_the_results_cant_be_read(tmp_path):
    """The elevated PR7-E ran (Windows said yes) but its results file couldn't be read: the disk decides."""
    (tmp_path / "moved here").mkdir()
    (tmp_path / "copied").write_bytes(b"x")
    (tmp_path / "still here").write_bytes(b"x")
    (tmp_path / "moved too").write_bytes(b"x")
    got = elevated.check_done([
        {"what": "move", "src": str(tmp_path / "gone"), "target": str(tmp_path / "moved here")},
        {"what": "copy", "src": str(tmp_path / "a"), "target": str(tmp_path / "copied")},
        {"what": "copy", "src": str(tmp_path / "a"), "target": str(tmp_path / "missing")},
        {"what": "move", "src": str(tmp_path / "still here"), "target": str(tmp_path / "moved too")},
    ])
    assert got[0] is None and got[1] is None and got[2] and got[3]


def test_the_exchange_folder_is_a_plain_temp_folder():
    import tempfile
    f = elevated.exchange_folder()
    try:
        assert os.path.isdir(f) and os.path.dirname(f) == tempfile.gettempdir()
        assert os.path.basename(f).startswith("pr7e-admin-")
    finally:
        shutil.rmtree(f, ignore_errors=True)


def installed(folder, state, name="Behemoth Bass.vst3"):
    """A plugin already in the protected folder (put there "as administrator")."""
    state["admin"] = True
    try:
        b = folder / name
        (b / "Contents").mkdir(parents=True)
        (b / "Contents" / "moduleinfo.json").write_text("{}")
    finally:
        state["admin"] = False
    return b


def test_delete_to_trash_in_a_protected_folder_asks_once(tmp_path, protected):
    """The user's Program Files VST3 folder: Explorer asks for administrator permission and deletes; so does PR7-E."""
    from pr7e import trash
    folder, state, elevate = protected
    b = installed(folder, state)
    asked = []
    job = ops.TrashJob([str(b)])
    job.ask_admin = lambda what: asked.append(what) or True
    job.elevate = elevate
    job.run()
    assert job.state == "done" and job.errors == [] and job.untrashable == []
    assert asked == [(1, str(folder), "delete")] and state["elevated"] == 1
    assert not b.exists()
    assert [i.original for i in job.items] == [str(b)]                        # in Trash, so Undo can restore
    assert any(os.path.normcase(i.original) == os.path.normcase(str(b)) for i in trash.items())
    assert isinstance(job.undo, ops.RestoreJob)


def test_delete_for_good_rename_and_new_folder_in_a_protected_folder(tmp_path, protected):
    folder, state, elevate = protected
    for job, check, verb in [
        (ops.RenameJob(str(installed(folder, state, "Old.vst3")), "New.vst3"),
         lambda: (folder / "New.vst3").is_dir() and not (folder / "Old.vst3").exists(), "rename"),
        (ops.NewFolderJob(str(folder), "Presets"), lambda: (folder / "Presets").is_dir(), "create"),
        (ops.DeleteJob([str(installed(folder, state))]), lambda: not (folder / "Behemoth Bass.vst3").exists(), "delete"),
    ]:
        asked = []
        job.ask_admin = lambda what, asked=asked: asked.append(what) or True
        job.elevate = elevate
        job.run()
        assert job.state == "done" and job.errors == [], (verb, job.errors)
        assert check(), verb
        assert len(asked) == 1 and asked[0][2] == verb


def test_declining_leaves_protected_items_alone(tmp_path, protected):
    folder, state, elevate = protected
    b = installed(folder, state)
    job = ops.TrashJob([str(b)])
    job.ask_admin = lambda what: False
    job.elevate = elevate
    job.run()
    assert b.exists() and state["elevated"] == 0
    assert job.untrashable == []                       # not offered for deleting for good instead
    assert job.errors == ["“Behemoth Bass.vst3”: Access is denied"]
    job = ops.RenameJob(str(b), "Other.vst3")
    job.ask_admin = lambda what: False
    job.run()
    assert job.errors and "Access is denied" in job.errors[0] and b.exists() and not (folder / "Other.vst3").exists()
