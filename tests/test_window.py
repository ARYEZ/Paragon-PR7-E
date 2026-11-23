"""The real window, offscreen, on a temporary home folder: what each action does to the files."""
import os
import time

import pytest
from PySide6.QtCore import QMimeData, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtWidgets import QApplication

from pr7e import places, trash


def wait(cond, timeout=10):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        QApplication.processEvents()
        if cond():
            return True
        time.sleep(0.01)
    QApplication.processEvents()
    return cond()


def names(w):
    return [e.name for e in w.tab.model.rows]


@pytest.fixture
def home(tmp_path, monkeypatch):
    h = tmp_path / "home"
    for d in ("Documents", "Music", "Pictures"):
        (h / d).mkdir(parents=True)
    (h / "Music" / "Paragon" / "ELSEWORLD").mkdir(parents=True)
    for n in ("01 Portal.flac", "02 Ruins of Elsewhere.flac", "03 Static Bloom.mp3"):
        (h / "Music" / "Paragon" / "ELSEWORLD" / n).write_bytes(os.urandom(2000))
    (h / "Documents" / "notes.txt").write_bytes(b"notes")
    (h / "Documents" / "elseworld mix notes.txt").write_bytes(b"mix")
    monkeypatch.setattr(places, "home", lambda: str(h))
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path / "data"))
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path / "config"))
    return h


@pytest.fixture
def win(qapp, clean_settings, home, monkeypatch):
    from pr7e import system
    from pr7e.main_window import MainWindow
    opened = []
    monkeypatch.setattr(system, "open_path", lambda p: opened.append(p))
    w = MainWindow([str(home)])
    w.opened = opened
    w.sync_jobs = True
    w.messages = []
    w.answers = {"message": lambda p: w.messages.append(p), "confirm": lambda p: True}
    w.show()
    QApplication.processEvents()
    yield w
    w.runner.wait()
    w.answers["confirm"] = lambda p: True
    w.close()
    QApplication.processEvents()


def test_navigation_history_and_path_bar(win, home):
    w = win
    assert names(w) == ["Documents", "Music", "Pictures"]
    assert w.headerbar.path.segments == [("home", "Home", str(home))]
    w.go(str(home / "Music" / "Paragon"))
    assert names(w) == ["ELSEWORLD"]
    assert [s[1] for s in w.headerbar.path.segments] == ["Home", "Music", "Paragon"]
    assert w.windowTitle() == "Paragon"
    w.back()
    assert w.tab.location == str(home) and w.tab.forward == [str(home / "Music" / "Paragon")]
    w.forward()
    assert w.tab.location == str(home / "Music" / "Paragon")
    w.up()
    assert w.tab.location == str(home / "Music")
    assert [e.name for e in w.tab.selected()] == ["Paragon"]  # going up selects where you came from
    w.headerbar.path.go.emit(str(home))
    assert w.tab.location == str(home)
    # Ctrl+L: type a path (a file's path opens its folder with it selected).
    w.edit_location()
    assert w.headerbar.location.isVisible() and w.headerbar.location.text() == str(home)
    w.headerbar.location.setText(str(home / "Documents" / "notes.txt"))
    w._location_entered()
    assert w.tab.location == str(home / "Documents") and [e.name for e in w.tab.selected()] == ["notes.txt"]
    assert w.headerbar.path.isVisible()


def test_folder_item_counts_and_selection_label(win, home):
    w = win
    assert wait(lambda: all(e.size >= 0 for e in w.tab.model.rows))
    sizes = {e.name: e.size for e in w.tab.model.rows}
    assert sizes == {"Documents": 2, "Music": 1, "Pictures": 0}
    w.go(str(home / "Music" / "Paragon" / "ELSEWORLD"))
    w.tab.select_paths([e.path for e in w.tab.model.rows[:2]])
    assert w.status.text() == "2 items selected (4.0 kB)"
    w.tab.select_paths([w.tab.model.rows[0].path])
    assert w.status.text() == "“01 Portal.flac” selected (2.0 kB)"
    w.tab.view.clearSelection()
    assert not w.status.isVisible()


def test_open_folders_and_files(win, home):
    w = win
    w.tab.select_paths([str(home / "Documents")])
    w.open_selected()
    assert w.tab.location == str(home / "Documents")
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w.open_selected()
    assert w.opened == [str(home / "Documents" / "notes.txt")]


def test_tabs(win, home):
    w = win
    assert not w.tabbar.isVisible()
    t2 = w.new_tab(str(home / "Music"))
    assert w.tab is t2 and w.tabbar.isVisible() and [t for _i, t in w.tabbar.tabs] == ["Home", "Music"]
    w.set_current_tab(0)
    assert w.tab.location == str(home) and names(w) == ["Documents", "Music", "Pictures"]
    w.close_tab(1)
    assert len(w.tabs) == 1 and not w.tabbar.isVisible()
    # Open a folder in a new tab (Ctrl+Return).
    w.tab.select_paths([str(home / "Pictures")])
    w.open_selected(new_tab=True)
    assert [t.location for t in w.tabs] == [str(home), str(home / "Pictures")]


def test_views_and_zoom(win, home):
    w = win
    w.tab.select_paths([str(home / "Music")])
    w.set_view("list")
    assert w.tab.view is w.tab.list and [e.name for e in w.tab.selected()] == ["Music"]
    assert w.tab.model.columns() == ["NAME", "SIZE", "MODIFIED", ""]
    w.set_view("grid")
    assert w.tab.view is w.tab.grid
    w.set_zoom(4)
    assert w.tab.grid.delegate.icon == 192
    w.set_zoom(99)
    assert w.zoom == 4


def test_copy_paste_between_tabs_and_undo(win, home):
    w = win
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w.copy()
    md = QGuiApplication.clipboard().mimeData()
    assert [os.path.normpath(u.toLocalFile()) for u in md.urls()] == [str(home / "Documents" / "notes.txt")]
    assert bytes(md.data("x-special/gnome-copied-files")).startswith(b"copy\n")
    w.new_tab(str(home / "Pictures"))
    w.paste()
    assert (home / "Pictures" / "notes.txt").read_bytes() == b"notes"
    assert names(w) == ["notes.txt"] and [e.name for e in w.tab.selected()] == ["notes.txt"]
    # Paste again into the folder it came from: a copy.
    w.set_current_tab(0)
    w.paste()
    assert sorted(os.listdir(home / "Documents")) == ["elseworld mix notes.txt", "notes (copy).txt", "notes.txt"]
    w.undo()
    assert sorted(os.listdir(home / "Documents")) == ["elseworld mix notes.txt", "notes.txt"]
    w.redo()
    assert (home / "Documents" / "notes (copy).txt").exists()


def test_cut_paste_moves(win, home):
    w = win
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w.copy(cut=True)
    w.go(str(home / "Pictures"))
    w.paste()
    assert not (home / "Documents" / "notes.txt").exists() and (home / "Pictures" / "notes.txt").exists()
    assert win.clipboard_files() == ([], False)  # a cut pastes once
    w.undo()
    assert (home / "Documents" / "notes.txt").exists() and not (home / "Pictures" / "notes.txt").exists()


def test_paste_files_copied_in_explorer(win, home):
    """Explorer puts CF_HDROP (urls) and "Preferred DropEffect" on the clipboard: 2 = cut."""
    w = win
    md = QMimeData()
    md.setUrls([QUrl.fromLocalFile(str(home / "Documents" / "notes.txt"))])
    md.setData('application/x-qt-windows-mime;value="Preferred DropEffect"', b"\x02\x00\x00\x00")
    QGuiApplication.clipboard().setMimeData(md)
    assert w.clipboard_files() == ([str(home / "Documents" / "notes.txt")], True)
    w.go(str(home / "Music"))
    w.paste()
    assert (home / "Music" / "notes.txt").exists() and not (home / "Documents" / "notes.txt").exists()


def test_drop(win, home):
    w = win
    src = str(home / "Documents" / "notes.txt")
    w.drop([src], str(home / "Music"))           # same drive: moves
    assert (home / "Music" / "notes.txt").exists() and not os.path.exists(src)
    w.drop([str(home / "Music" / "notes.txt")], str(home / "Pictures"), "copy")   # Ctrl: copies
    assert (home / "Music" / "notes.txt").exists() and (home / "Pictures" / "notes.txt").exists()
    w.drop([str(home / "Pictures" / "notes.txt")], "trash:")
    assert not (home / "Pictures" / "notes.txt").exists() and [i.name for i in trash.items()] == ["notes.txt"]


def test_rename_new_folder_and_undo(win, home):
    w = win
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    asked = []
    w.answers["name"] = lambda p: (asked.append(p), "ideas.txt")[1]
    w.rename()
    assert asked[0]["initial"] == "notes.txt" and asked[0]["button"] == "Rename"
    assert sorted(os.listdir(home / "Documents")) == ["elseworld mix notes.txt", "ideas.txt"]
    assert [e.name for e in w.tab.selected()] == ["ideas.txt"]
    w.undo()
    assert (home / "Documents" / "notes.txt").exists()
    w.answers["name"] = lambda p: "Projects"
    w.new_folder()
    assert (home / "Documents" / "Projects").is_dir() and [e.name for e in w.tab.selected()] == ["Projects"]
    assert w._main_menu and w.undo_stack[-1].undo_label == "Create Folder"


def test_trash_restore_and_empty(win, home):
    w = win
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w.act["trash"].trigger()
    assert not (home / "Documents" / "notes.txt").exists()
    w.go("trash:")
    assert names(w) == ["notes.txt"] and w.tab.model.rows[0].location == "Home/Documents"
    assert w.trashbar.isVisible()
    assert w.tab.model.columns() == ["NAME", "ORIGINAL LOCATION", "SIZE", "DELETED"]
    w.tab.select_paths([w.tab.model.rows[0].path])
    w.trashbar.restore.emit()
    assert (home / "Documents" / "notes.txt").read_bytes() == b"notes" and names(w) == []
    # Undo of the Delete key restores too.
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w.trash_selected()
    w.undo()
    assert (home / "Documents" / "notes.txt").exists()
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w.trash_selected()
    confirm = []
    w.answers["confirm"] = lambda p: (confirm.append(p["title"]), False)[1]
    w.empty_trash()
    assert len(trash.items()) == 1 and confirm == ["Empty Trash"]
    w.answers["confirm"] = lambda p: True
    w.empty_trash()
    assert trash.items() == []


def test_delete_permanently_asks(win, home):
    w = win
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w.answers["confirm"] = lambda p: False
    w.delete_selected()
    assert (home / "Documents" / "notes.txt").exists()
    w.answers["confirm"] = lambda p: True
    w.delete_selected()
    assert not (home / "Documents" / "notes.txt").exists() and trash.items() == []


def test_untrashable_items_ask_to_delete(win, home, monkeypatch):
    def no_trash(path, b=None):
        raise trash.TrashError("no Trash on this drive")
    monkeypatch.setattr(trash, "send", no_trash)
    w = win
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    asked = []
    w.answers["confirm"] = lambda p: (asked.append(p["heading"]), True)[1]
    w.trash_selected()
    assert "can’t be put in the Trash" in asked[0] and not (home / "Documents" / "notes.txt").exists()


def test_search(win, home):
    w = win
    w.start_search("elsew")
    w._run_search(sync=True)
    QApplication.processEvents()
    found = {e.name: e.location for e in w.tab.model.rows}
    assert found == {"elseworld mix notes.txt": "Documents", "ELSEWORLD": "Music/Paragon",
                     "02 Ruins of Elsewhere.flac": "Music/Paragon/ELSEWORLD"}
    assert w.tab.model.columns()[1] == "LOCATION" and w.filters.isVisible()
    w.filters.what.set_value("Music")
    w._run_search(sync=True)
    QApplication.processEvents()
    assert names(w) == ["02 Ruins of Elsewhere.flac"]
    w.open_location(w.tab.model.rows[0])
    assert w.tab.location == str(home / "Music" / "Paragon" / "ELSEWORLD") and w.tab.search is None
    assert [e.name for e in w.tab.selected()] == ["02 Ruins of Elsewhere.flac"]
    w.start_search("zzz-nothing")
    w._run_search(sync=True)
    QApplication.processEvents()
    assert names(w) == [] and w.empty.title == "No Results Found"
    w.end_search()
    assert w.tab.search is None and len(names(w)) == 3 and w.headerbar.path.isVisible()


def test_typing_starts_a_search(win, home):
    w = win
    w.tab.view.typed.emit("m")
    assert w.tab.search is not None and w.headerbar.search.text() == "m"
    w._run_search(sync=True)
    QApplication.processEvents()
    assert "Music" in names(w)


def test_hidden_files_and_sorting(win, home):
    w = win
    (home / ".config").mkdir()
    w.reload()
    assert ".config" not in names(w)
    w.toggle_hidden()
    assert ".config" in names(w)
    w.toggle_hidden()
    w.set_sort("name_desc")
    assert names(w) == ["Pictures", "Music", "Documents"]
    w._sort_column("name")
    assert w.sort == "name" and names(w) == ["Documents", "Music", "Pictures"]


def test_stars_follow_renames(win, home):
    w = win
    w.go(str(home / "Documents"))
    p = str(home / "Documents" / "notes.txt")
    w.star([p], True)
    assert w.tab.model.starred(w.tab.model.rows[w.tab.model.row_of(p)])
    w.tab.select_paths([p])
    w.answers["name"] = lambda _p: "renamed.txt"
    w.rename()
    w.go("starred:")
    assert names(w) == ["renamed.txt"] and w.tab.model.rows[0].location == "Home/Documents"


def test_watcher_sees_outside_changes(win, home):
    w = win
    (home / "made elsewhere.txt").write_bytes(b"x")
    assert wait(lambda: "made elsewhere.txt" in names(w))


def test_other_locations_and_bookmarks(win, home):
    w = win
    w.go("other:")
    assert w.tab.model.rows and all(e.kind in ("drive", "usb") for e in w.tab.model.rows)
    w.go(str(home / "Music"))
    w.bookmark()
    assert any(pl.kind == "bookmark" and pl.label == "Music" for pl in w.sidebar.model().rows)
    w.go(str(home))
    w.sidebar.activated_place.emit(next(pl for pl in w.sidebar.model().rows if pl.kind == "bookmark"), False)
    assert w.tab.location == str(home / "Music")


def test_menus(win, home):
    w = win
    menus = []
    w.answers["context"] = menus.append
    w.answers["menu"] = menus.append
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w._item_menu(w.tab.selected())
    texts = [a.text() for a in menus[-1].actions() if a.text()]
    for t in ("Cut", "Copy", "Move to…", "Copy to…", "Move to Trash", "Rename…", "Star", "Properties",
              "Show More Options"):
        assert t in texts
    w._background_menu()
    assert [a.text() for a in menus[-1].actions() if a.text()][:3] == ["New Folder…", "Paste", "Select All"]
    w._main_menu()
    assert any(a.text() == "Show Hidden Files" for a in menus[-1].actions())


def test_move_to_picks_a_folder(win, home):
    w = win
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w.answers["choose_folder"] = lambda p: str(home / "Music")
    w.copy_to(move=True)
    assert (home / "Music" / "notes.txt").exists() and not (home / "Documents" / "notes.txt").exists()


def test_properties_counts_folders(win, home):
    w = win
    dlgs = []
    w.answers["properties"] = dlgs.append
    w.tab.select_paths([str(home / "Music")])
    w.properties()
    d = dlgs[0]
    assert wait(lambda: d.counted is not None)
    d._show_count()
    assert d.size.text() == "5 items, totalling 6.0 kB"   # Paragon, ELSEWORLD and its 3 files
    d.done(0)


def test_finishes(win):
    from pr7e import theme
    w = win
    w.set_finish("aurora")
    assert theme.FINISH == "aurora"
    w.set_finish("obsidian")
    assert theme.FINISH == "obsidian"
