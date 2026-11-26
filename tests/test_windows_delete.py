"""Sending a folder to the real Recycle Bin through the real window, after the things PR7-E does with folders
(Windows CI only). Explorer deletes these folders at once; PR7-E must too, whatever it has looked at before."""
import os
import sys

import pytest
from PySide6.QtGui import QColor, QImage
from PySide6.QtWidgets import QApplication

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


def trash_it(w, f):
    w.go(str(f.parent), tab=w.tab)
    spin()
    w.tab.select_paths([str(f)])
    w.trash_selected()
    w.runner.wait()
    spin()
    return w.messages


@pytest.mark.parametrize("before", ["nothing", "browsed_inside", "open_in_another_tab", "expanded"])
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
        win.set_view("list")
        entry = next(e for e in win.tab.model.rows if e.path == str(f))
        win.expand_folder(win.tab, entry, True)
        spin(1)
    messages = trash_it(win, f)
    assert not f.exists(), f"{before}: still there; PR7-E said {messages}"


@pytest.mark.parametrize("watched", ["the folder", "a folder inside", "nothing"])
def test_a_watched_folder_and_the_recycle_bin(qapp, folder, real_bin, watched):
    """Qt's QFileSystemWatcher on its own (no window): does a watch on the folder or inside it stop Windows?"""
    from PySide6.QtCore import QFileSystemWatcher
    _root, f = folder
    w = QFileSystemWatcher()
    if watched == "the folder":
        w.addPath(str(f))
    elif watched == "a folder inside":
        w.addPath(str(f / "inside"))
    spin(0.3)
    from pr7e import trash
    try:
        real_bin.send(str(f))
        outcome = "sent"
    except trash.TrashError as e:
        outcome = f"refused: {e}"
    w.removePaths(w.directories())
    print(f"watched {watched}: {outcome}")
    assert outcome == "sent", f"watched {watched}: {outcome}"
