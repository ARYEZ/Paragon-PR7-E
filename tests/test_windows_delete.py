"""Sending a folder to the real Recycle Bin through the real window, after the things PR7-E does with folders
(Windows CI only). Explorer deletes these folders at once; PR7-E must too, whatever it has looked at before."""
import os
import sys

import pytest
from PySide6.QtGui import QColor, QImage
from PySide6.QtWidgets import QApplication

from pr7e.main_window import load_prefs

pytestmark = pytest.mark.skipif(sys.platform != "win32" or not os.environ.get("CI"),
                                reason="the real Recycle Bin: Windows CI only")


@pytest.fixture
def real_bin(monkeypatch):
    from pr7e import trash
    monkeypatch.delenv("PR7E_TRASH", raising=False)
    monkeypatch.setattr(trash, "_backend", None)
    yield trash.backend()
    monkeypatch.setattr(trash, "_backend", None)


@pytest.fixture
def folder(tmp_path):
    root = tmp_path / f"pr7e-delete-{os.getpid()}"
    f = root / "Holiday"
    (f / "inside").mkdir(parents=True)
    for i in range(3):
        img = QImage(64, 48, QImage.Format.Format_RGB32)
        img.fill(QColor("#DE0639"))
        img.save(str(f / f"photo {i}.png"))
    (f / "notes.txt").write_bytes(b"notes")
    (f / "inside" / "more.txt").write_bytes(b"more")
    return root, f


@pytest.fixture
def win(qapp, clean_settings, folder):
    from pr7e.main_window import MainWindow
    root, _f = folder
    w = MainWindow([str(root)])
    w.sync_jobs = True
    w.messages = []
    w.answers = {"message": lambda p: w.messages.append(p), "confirm": lambda p: True}
    w.show()
    QApplication.processEvents()
    yield w
    w.runner.wait()
    w.close()
    w.deleteLater()
    QApplication.processEvents()


def spin(seconds=0.5):
    import time
    end = time.time() + seconds
    while time.time() < end:
        QApplication.processEvents()
        time.sleep(0.01)


def row(w, path, timeout=10):
    """The list's row for `path`, once it is there (an expanded folder's contents load in the background)."""
    import time
    want = os.path.normcase(str(path))
    end = time.time() + timeout
    while time.time() < end:
        for e in w.tab.model.rows:
            if os.path.normcase(e.path) == want:
                return e
        spin(0.05)
    raise AssertionError(f"no row for {path}: {[e.path for e in w.tab.model.rows]}")


def trash_it(w, f):
    w.go(str(f.parent), tab=w.tab)
    spin()
    w.tab.select_paths([str(f)])
    w.trash_selected()
    w.runner.wait()
    spin()
    return w.messages


@pytest.mark.parametrize("before", ["nothing", "browsed_inside", "open_in_another_tab", "expanded",
                                    "subfolder_in_another_tab", "subfolder_expanded", "subfolder_tab_delete_for_good"])
def test_a_folder_goes_to_the_recycle_bin(win, folder, real_bin, before):
    root, f = folder
    if before == "browsed_inside":           # thumbnails and counts of what's inside were loaded
        win.go(str(f), tab=win.tab)
        spin(2)
        win.go(str(root), tab=win.tab)
    elif before == "open_in_another_tab":
        win.new_tab(str(f), switch=False)
        spin(1)
    elif before == "expanded":
        win.set_prefs({**load_prefs(), "expand_folders": True})    # Preferences > Expandable folders
        win.set_view("list")
        win.expand_folder(win.tab, row(win, f), True)
        spin(1)
    elif before in ("subfolder_in_another_tab", "subfolder_tab_delete_for_good"):   # the case that failed
        win.new_tab(str(f / "inside"), switch=False)
        spin(1)
    elif before == "subfolder_expanded":
        win.set_prefs({**load_prefs(), "expand_folders": True})
        win.set_view("list")
        win.expand_folder(win.tab, row(win, f), True)
        spin(1)
        win.expand_folder(win.tab, row(win, f / "inside"), True)
        spin(1)
    held = {"open_in_another_tab": f, "expanded": f, "subfolder_in_another_tab": f / "inside",
            "subfolder_expanded": f / "inside", "subfolder_tab_delete_for_good": f / "inside"}.get(before)
    if held is not None:      # the set-up really watches what it means to
        assert os.path.normcase(str(held)) in {os.path.normcase(d) for d in win.watcher.directories()}, \
            win.watcher.directories()
    if before == "subfolder_tab_delete_for_good":
        win.tab.select_paths([str(f)])
        win.delete_selected()
        win.runner.wait()
        spin()
        assert not f.exists(), f"{before}: still there; PR7-E said {win.messages}"
        return
    messages = trash_it(win, f)
    assert not f.exists(), f"{before}: still there; PR7-E said {messages}"


@pytest.mark.parametrize("watched", ["the folder", "a folder inside, released first", "nothing"])
def test_a_watched_folder_and_the_recycle_bin(qapp, folder, real_bin, watched):
    """Qt's QFileSystemWatcher on its own. A watch on a folder inside stops Windows (found on CI: "can't be put in the
    Trash", Windows' own "administrator permission" box for the user); removing the watch first lets it through."""
    from PySide6.QtCore import QFileSystemWatcher
    _root, f = folder
    w = QFileSystemWatcher()
    if watched == "the folder":
        w.addPath(str(f))
    elif watched != "nothing":
        w.addPath(str(f / "inside"))
    spin(0.3)
    if watched.endswith("released first"):
        w.removePaths(w.directories())
    from pr7e import trash
    try:
        real_bin.send(str(f))
        outcome = "sent"
    except trash.TrashError as e:
        outcome = f"refused: {e}"
    w.removePaths(w.directories())
    print(f"watched {watched}: {outcome}")
    assert outcome == "sent", f"watched {watched}: {outcome}"
