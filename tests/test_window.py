"""The real window, offscreen, on a temporary home folder: what each action does to the files."""
import os
import sys
import time

import pytest
from PySide6.QtCore import QMimeData, Qt, QUrl
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
    assert w.tab.model.rows and all(e.kind in ("drive", "usb") for e in w.tab.model.rows[:-1])
    assert (w.tab.model.rows[-1].name, w.tab.model.rows[-1].path) == ("Network", "network:")
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
    assert [a.text() for a in menus[-1].actions() if a.text()][:4] == ["New Folder…", "New Document", "Paste",
                                                                        "Select All"]
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


def test_compress_and_extract_from_the_menu(win, home, monkeypatch):
    monkeypatch.setenv("PR7E_7Z", os.devnull + "-missing")   # Python's zip: the same on every system
    w = win
    w.go(str(home / "Music" / "Paragon"))
    w.tab.select_paths([str(home / "Music" / "Paragon" / "ELSEWORLD")])
    asked = []
    w.answers["compress"] = lambda p: (asked.append(p), ("ELSEWORLD", ".zip", ""))[1]
    w.compress()
    assert asked[0]["name"] == "ELSEWORLD" and asked[0]["have_7z"] is False
    z = home / "Music" / "Paragon" / "ELSEWORLD.zip"
    assert z.is_file() and [e.name for e in w.tab.selected()] == ["ELSEWORLD.zip"]
    menus = []
    w.answers["context"] = menus.append
    w._item_menu(w.tab.selected())
    texts = [a.text() for a in menus[-1].actions()]
    assert "Extract Here" in texts and "Extract to…" in texts and "Compress…" in texts
    w.answers["choose_folder"] = lambda p: str(home / "Documents")
    w.extract(str(z), choose=True)
    assert sorted(os.listdir(home / "Documents" / "ELSEWORLD")) == ["01 Portal.flac", "02 Ruins of Elsewhere.flac",
                                                                  "03 Static Bloom.mp3"]
    assert (home / "Documents" / "ELSEWORLD" / "01 Portal.flac").read_bytes() == \
        (home / "Music" / "Paragon" / "ELSEWORLD" / "01 Portal.flac").read_bytes()
    w.undo()
    assert not (home / "Documents" / "ELSEWORLD").exists()


def test_compress_dialog(qapp, tmp_path):
    from pr7e.dialogs import CompressDialog
    (tmp_path / "Taken.zip").write_bytes(b"x")
    d = CompressDialog(str(tmp_path), "Taken", have_7z=False)
    assert not d.ok.isEnabled() and "already exists" in d.problem.text()
    d.name.setText("New")
    assert d.ok.isEnabled() and d.answer() == ("New", ".zip", "")
    model = d.fmt.model()
    assert not model.item(d.fmt.findText(".7z")).isEnabled()       # needs 7-Zip
    d2 = CompressDialog(str(tmp_path), "New", have_7z=True)
    d2.fmt.setCurrentText("Encrypted .zip")
    assert d2.password.isVisibleTo(d2) and not d2.ok.isEnabled()  # a password first
    d2.password.setText("pw")
    assert d2.ok.isEnabled() and d2.answer() == ("New", "Encrypted .zip", "pw")


def test_show_more_options_needs_one_folder(win, home):
    from pr7e import winshell
    w = win
    menus = []
    w.answers["context"] = menus.append
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w._item_menu(w.tab.selected())
    more = next(a for a in menus[-1].actions() if a.text() == "Show More Options")
    assert more.isEnabled() == winshell.available


def test_rename_several(win, home):
    w = win
    d = home / "Music" / "Paragon" / "ELSEWORLD"
    w.go(str(d))
    w.select_all()
    asked = []

    def answer(paths):
        asked.append(paths)
        from pr7e.dialogs import BatchRenameDialog
        dlg = BatchRenameDialog(paths, w)
        dlg.use_replace.setChecked(True)
        dlg.find.setText("0")
        dlg.replace_with.setText("Track ")
        assert dlg.ok.isEnabled() and dlg.preview.topLevelItemCount() == 3
        return dlg.answer()
    w.answers["batch_rename"] = answer
    w.rename()
    assert len(asked[0]) == 3
    assert sorted(os.listdir(d)) == ["Track 1 Portal.flac", "Track 2 Ruins of Elsewhere.flac",
                                     "Track 3 Static Bloom.mp3"]
    assert sorted(e.name for e in w.tab.selected()) == sorted(os.listdir(d))
    w.undo()
    assert sorted(os.listdir(d)) == ["01 Portal.flac", "02 Ruins of Elsewhere.flac", "03 Static Bloom.mp3"]


def test_preferences(win, home):
    from pr7e.dialogs import PreferencesDialog
    from pr7e.main_window import PREFS, load_prefs
    w = win
    assert load_prefs() == PREFS
    (home / "a.txt").write_text("a")
    w.reload()
    assert names(w) == ["Documents", "Music", "Pictures", "a.txt"]
    d = PreferencesDialog(dict(w.prefs), w)
    d.folders_first.setChecked(False)
    d.show_delete.setChecked(True)
    d.search_subfolders.setChecked(False)
    d.click.setCurrentIndex(d.click.findData("single"))
    d.combos["counts"].setCurrentIndex(d.combos["counts"].findData("never"))
    w.answers["preferences"] = lambda p: d.answer()
    w.preferences()
    assert load_prefs()["click"] == "single" and w.prefs["counts"] == "never"
    assert names(w) == ["a.txt", "Documents", "Music", "Pictures"]      # folders no longer first
    assert all(e.size == -1 for e in w.tab.model.rows if e.is_dir)       # no counting
    menus = []
    w.answers["context"] = menus.append
    w.tab.select_paths([str(home / "a.txt")])
    w._item_menu(w.tab.selected())
    assert "Delete Permanently…" in [a.text() for a in menus[-1].actions()]
    w.start_search("notes")
    w._run_search(sync=True)
    QApplication.processEvents()
    assert names(w) == []                                                 # not in subfolders
    w.end_search()
    # Single click opens.
    idx = w.tab.model.index(w.tab.model.row_of(str(home / "Documents")), 0)
    w.tab.view.clicked.emit(idx)
    assert w.tab.location == str(home / "Documents")


def test_connect_to_server(win, home, server, server_root):
    from pr7e import servers
    w = win
    w.go("other:")
    assert w.connectbar.isVisible()
    asked = []
    w.answers["login"] = lambda p: (asked.append(p), {"anonymous": False, "user": "paragon",
                                                      "password": server.password, "remember": True})[1]
    w.answers["hostkey"] = lambda p: "always"
    w.answers["cert"] = lambda p: True
    clashes = []
    w.answers["conflict"] = lambda c: (clashes.append(c.target), ("replace", False))[1]
    w.connectbar.address.setText(server.address.replace("paragon@", ""))
    w.connectbar._go()
    assert asked and asked[0]["host"] == "127.0.0.1"
    loc = w.tab.location
    assert servers.is_remote(loc) and sorted(names(w)) == ["index.html", "music"]
    k = servers.split(loc)[0]
    assert servers.saved_password(k) == server.password and servers.recent()[0] == k
    assert any(pl.kind == "server" for pl in w.sidebar.model().rows)
    assert w.headerbar.path.segments[0][0] == "server"
    # Into a folder; download by copy and paste into a local tab.
    w.go(servers.join(k, "/music"))
    assert sorted(names(w)) == ["elseworld", "notes.txt"]
    w.tab.select_paths([servers.join(k, "/music/elseworld")])
    w.copy()
    w.new_tab(str(home / "Documents"))
    w.paste()
    assert (home / "Documents" / "elseworld" / "01 Portal.flac").read_bytes() == \
        (server_root / "music" / "elseworld" / "01 Portal.flac").read_bytes()
    # Upload by dropping a local file on the server folder.
    w.set_current_tab(0)
    w.drop([str(home / "Documents" / "notes.txt")], w.tab.location)
    assert clashes == ["/music/notes.txt"]                                    # Files' question came up
    assert (server_root / "music" / "notes.txt").read_bytes() == b"notes"   # and the answer replaced it
    # New folder, rename, delete on the server.
    w.answers["name"] = lambda p: "Made"
    w.new_folder()
    assert (server_root / "music" / "Made").is_dir() and "Made" in names(w)
    w.tab.select_paths([servers.join(k, "/music/Made")])
    w.answers["name"] = lambda p: (p.get("taken") is not None and "Renamed")
    w.rename()
    assert (server_root / "music" / "Renamed").is_dir()
    w.tab.select_paths([servers.join(k, "/music/Renamed")])
    confirm = []
    w.answers["confirm"] = lambda p: (confirm.append(p["text"]), True)[1]
    w.trash_selected()        # servers have no Trash: deletes for good, after asking
    assert not (server_root / "music" / "Renamed").exists() and confirm[0].startswith("Servers have no Trash")
    # Open: downloaded, then opened.
    w.tab.select_paths([servers.join(k, "/music/notes.txt")])
    w.open_selected()
    assert w.opened and open(w.opened[-1], "rb").read() == b"notes"
    # Disconnect: the tab goes to Other Locations.
    w.disconnect_server(k + "/")
    assert w.tab.location == "other:" and k not in servers.CONNECTED
    assert not any(pl.kind == "server" for pl in w.sidebar.model().rows)


def connected(w, server):
    from pr7e import servers
    w.answers["hostkey"] = lambda p: "always"
    w.answers["cert"] = lambda p: True
    w.connect_server(server.address.replace("paragon@", "paragon:" + server.password + "@"))
    return servers.split(w.tab.location)[0]


def test_server_search_properties_and_relay(win, home, server, server_root):
    from pr7e import servers
    w = win
    k = connected(w, server)
    (server_root / "backup").mkdir()
    w.reload()
    # Search runs on the server, with LOCATION and no star column.
    w.start_search("portal")
    w._run_search(sync=True)
    QApplication.processEvents()
    assert [(e.name, e.location) for e in w.tab.model.rows] == [("01 Portal.flac", "music/elseworld")]
    assert w.tab.model.columns() == ["NAME", "LOCATION", "SIZE", "MODIFIED"]
    w.open_location(w.tab.model.rows[0])
    assert w.tab.location == servers.join(k, "/music/elseworld") and w.tab.search is None
    assert [e.name for e in w.tab.selected()] == ["01 Portal.flac"]
    # Properties: counted on the server.
    w.go(servers.join(k, "/"))
    w.tab.select_paths([servers.join(k, "/music")])
    dlgs = []
    w.answers["properties"] = dlgs.append
    w.properties()
    d = dlgs[0]
    assert wait(lambda: d.counted is not None)
    d._show_count()
    assert d.size.text() == f"3 items, totalling {__import__('pr7e.fs', fromlist=['x']).human_size(300_012)}"
    d.done(0)
    # Copy and paste between two server folders: down and up again.
    w.copy()
    w.paste(servers.join(k, "/backup"))
    assert (server_root / "backup" / "music" / "elseworld" / "01 Portal.flac").read_bytes() == \
        (server_root / "music" / "elseworld" / "01 Portal.flac").read_bytes()


def test_network_share_login(win, home, monkeypatch, clean_settings):
    from pr7e import places
    w = win
    calls = []

    def connect_share(unc, user, password, remember):
        calls.append((unc, user, password, remember))
        raise OSError(1326, "The user name or password is wrong.", unc)
    monkeypatch.setattr(places, "connect_share", connect_share)
    asked = []
    w.answers["login"] = lambda p: (asked.append(p), {"user": "me", "password": "pw", "remember": True})[1]
    w.go("other:")
    w.connectbar.address.setText("smb://nas/music")
    w.connectbar._go()
    assert asked[0]["protocol"] == "smb" and asked[0]["host"] == "nas"
    assert calls == [(r"\\nas\music", "me", "pw", True)]
    assert w.messages and "The user name or password is wrong." in w.messages[-1][2]
    assert places.shares() == []


@pytest.mark.skipif(sys.platform != "win32" or not os.environ.get("CI"), reason="a real share: Windows CI")
def test_real_network_share(win, home, tmp_path):
    from pr7e import places
    unc = "\\\\localhost\\" + str(tmp_path)[0] + "$" + str(tmp_path)[2:]
    if not os.path.isdir(unc):
        pytest.skip("administrative shares are off here")
    (tmp_path / "on the share.txt").write_text("x")
    w = win
    w.connect_server(unc)
    assert w.tab.location == unc and "on the share.txt" in names(w)
    segs = w.headerbar.path.segments
    assert (segs[0][0], segs[0][2]) == ("desktop", "\\\\localhost") and segs[1][0] == "share"  # computer, share
    assert any(pl.kind == "share" for pl in w.sidebar.model().rows)
    w.forget_share(places.share_root(unc))
    assert not any(pl.kind == "share" for pl in w.sidebar.model().rows)


def test_expand_folders_in_list_view(win, home):
    from pr7e.main_window import load_prefs
    w = win
    w.set_prefs({**load_prefs(), "expand_folders": True})
    w.set_view("list")
    assert w.tab.model.expandable
    music = str(home / "Music")
    w.expand_folder(w.tab, next(e for e in w.tab.model.rows if e.path == music))
    assert [(e.name, e.depth) for e in w.tab.model.rows] == [("Documents", 0), ("Music", 0), ("Paragon", 1),
                                                             ("Pictures", 0)]
    paragon = next(e for e in w.tab.model.rows if e.name == "Paragon")
    w.expand_folder(w.tab, paragon, True)
    assert [e.name for e in w.tab.model.rows if e.depth == 2] == ["ELSEWORLD"]
    # Items inside work like any other: copy one out to Documents.
    w.tab.select_paths([str(home / "Music" / "Paragon")])
    assert w.status.text().startswith("“Paragon” selected")
    # A change inside an open folder shows up.
    (home / "Music" / "new song.mp3").write_bytes(b"x")
    assert wait(lambda: "new song.mp3" in names(w))
    # Left on an item inside goes to its folder's row; closing Music closes Paragon too.
    w.expand_folder(w.tab, next(e for e in w.tab.model.rows if e.name == "new song.mp3"), False)
    assert [e.name for e in w.tab.selected()] == ["Music"]
    w.expand_folder(w.tab, next(e for e in w.tab.model.rows if e.name == "Music"), False)
    assert names(w) == ["Documents", "Music", "Pictures"] and w.tab.expanded == set()
    # The grid never shows rows from inside folders.
    w.expand_folder(w.tab, next(e for e in w.tab.model.rows if e.name == "Music"), True)
    w.set_view("grid")
    assert names(w) == ["Documents", "Music", "Pictures"]


def test_new_document_from_templates(win, home, monkeypatch):
    from pr7e import places
    w = win
    tpl = home / "Templates"
    (tpl / "Office").mkdir(parents=True)
    (tpl / "Letter.txt").write_text("Dear ...")
    (tpl / "Office" / "Budget.csv").write_text("a,b")
    (tpl / "desktop.ini").write_text("x")
    monkeypatch.setattr(places, "templates_dir", lambda: str(tpl))
    assert [(n, len(k)) for n, _p, k in places.templates()] == [("Letter", 0), ("Office", 1)]
    w.go(str(home / "Documents"))
    menus = []
    w.answers["context"] = menus.append
    w._background_menu()
    sub = next(a.menu() for a in menus[-1].actions() if a.text() == "New Document")
    texts = [a.text() for a in sub.actions() if a.text()]
    assert texts == ["Letter", "Office", "Empty Document", "Open Templates Folder"]
    renamed = []
    w.answers["name"] = lambda p: (renamed.append(p["initial"]), "To the label.txt")[1]
    next(a for a in sub.actions() if a.text() == "Letter").trigger()
    assert wait(lambda: (home / "Documents" / "To the label.txt").exists())
    assert renamed == ["Letter.txt"] and (home / "Documents" / "To the label.txt").read_text() == "Dear ..."
    w.answers["name"] = lambda p: None
    w.new_document(None)
    w.new_document(None)
    QApplication.processEvents()
    assert (home / "Documents" / "Untitled Document.txt").exists() and \
        (home / "Documents" / "Untitled Document (2).txt").exists()
    w.undo()
    assert not (home / "Documents" / "Untitled Document (2).txt").exists()


def test_set_as_background(win, home, monkeypatch):
    from pr7e import places
    w = win
    (home / "Pictures" / "dusk.jpg").write_bytes(b"jpg")
    set_to = []
    monkeypatch.setattr(places, "set_wallpaper", lambda p, persist=True: set_to.append(p) or True)
    w.go(str(home / "Pictures"))
    w.tab.select_paths([str(home / "Pictures" / "dusk.jpg")])
    menus = []
    w.answers["context"] = menus.append
    w._item_menu(w.tab.selected())
    next(a for a in menus[-1].actions() if a.text() == "Set as Background…").trigger()
    assert set_to == [str(home / "Pictures" / "dusk.jpg")]


@pytest.mark.skipif(sys.platform != "win32" or not os.environ.get("CI"), reason="Windows' wallpaper call: CI only")
def test_real_wallpaper_call(qapp, tmp_path):
    from PySide6.QtGui import QColor, QImage
    from pr7e import places
    img = QImage(64, 64, QImage.Format_RGB32)
    img.fill(QColor("#DE0639"))
    img.save(str(tmp_path / "w.bmp"))
    assert places.set_wallpaper(str(tmp_path / "w.bmp"), persist=False)


def test_mouse_back_and_forward_buttons(win, home):
    from PySide6.QtCore import QPointF, Qt
    from PySide6.QtGui import QMouseEvent
    w = win
    w.go(str(home / "Music"))
    w.go(str(home / "Music" / "Paragon"))
    view = w.tab.view.viewport()

    def click(button):
        for kind in (QMouseEvent.Type.MouseButtonPress, QMouseEvent.Type.MouseButtonRelease):
            ev = QMouseEvent(kind, QPointF(20, 20), QPointF(20, 20), button, button, Qt.NoModifier)
            QApplication.sendEvent(view, ev)
    click(Qt.BackButton)
    assert w.tab.location == str(home / "Music")
    click(Qt.BackButton)
    assert w.tab.location == str(home)
    click(Qt.ForwardButton)
    assert w.tab.location == str(home / "Music")
    # Also over the sidebar and the header, as in Files.
    for kind in (QMouseEvent.Type.MouseButtonPress, QMouseEvent.Type.MouseButtonRelease):
        QApplication.sendEvent(w.sidebar.viewport(), QMouseEvent(kind, QPointF(5, 5), QPointF(5, 5), Qt.BackButton,
                                                                 Qt.BackButton, Qt.NoModifier))
    assert w.tab.location == str(home)


def test_browse_the_network(win, home, monkeypatch):
    from pr7e import places
    w = win
    monkeypatch.setattr(places, "network_computers", lambda: [("NAS", r"\\NAS"), ("STUDIO-PC", r"\\STUDIO-PC")])
    refusals = []

    def shares(computer):
        if computer == r"\\STUDIO-PC" and not refusals:
            refusals.append(computer)
            raise OSError(5, "Access is denied.", computer)
        return [("music", "All the music"), ("photos", "")]
    monkeypatch.setattr(places, "computer_shares", shares)
    logins = []
    monkeypatch.setattr(places, "connect_share", lambda unc, u, p, r: logins.append((unc, u, p)))
    w.go("other:")
    w.tab.select_paths(["network:"])
    w.open_selected()
    assert w.tab.location == "network:" and [(e.name, e.kind) for e in w.tab.model.rows] == [
        ("NAS", "computer"), ("STUDIO-PC", "computer")]
    assert w.tab.model.columns() == ["NAME", "DESCRIPTION"]
    w.tab.select_paths([r"\\NAS"])
    w.open_selected()
    assert w.tab.location == r"\\NAS" and [(e.name, e.path, e.extra) for e in w.tab.model.rows] == [
        ("music", r"\\NAS\music", "All the music"), ("photos", r"\\NAS\photos", "")]
    assert [s[1] for s in w.headerbar.path.segments] == ["Network", "NAS"]
    assert w.windowTitle() == "NAS"
    w.up()
    assert w.tab.location == "network:"
    # A computer that wants a login first: asked, connected through IPC$, then listed.
    w.answers["login"] = lambda p: {"user": "me", "password": "pw", "remember": False}
    w.go(r"\\STUDIO-PC")
    assert logins == [(r"\\STUDIO-PC\IPC$", "me", "pw")] and [e.name for e in w.tab.model.rows] == ["music",
                                                                                                   "photos"]


def test_server_stars_thumbnails_and_counts(win, home, server, server_root):
    from PySide6.QtGui import QColor, QImage
    from pr7e import places, servers
    from pr7e.main_window import load_prefs
    img = QImage(300, 200, QImage.Format_RGB32)
    img.fill(QColor("#DE0639"))
    img.save(str(server_root / "music" / "cover.png"))
    w = win
    k = connected(w, server)
    w.go(servers.join(k, "/music"))
    # "On this computer only" (the default): nothing is downloaded or counted on servers.
    e = next(x for x in w.tab.model.rows if x.name == "elseworld")
    assert e.size == -1
    assert w.tab.model.data(w.tab.model.index(w.tab.model.row_of(servers.join(k, "/music/cover.png")), 0),
                            Qt.UserRole + 1) is None
    # "Always": counted and thumbnailed on the second connection.
    w.set_prefs({**load_prefs(), "thumbnails": "always", "counts": "always"})
    assert wait(lambda: next(x for x in w.tab.model.rows if x.name == "elseworld").size == 1)
    idx = w.tab.model.index(w.tab.model.row_of(servers.join(k, "/music/cover.png")), 0)
    w.tab.model.data(idx, Qt.UserRole + 1)
    assert wait(lambda: w.tab.model.data(idx, Qt.UserRole + 1) is not None)
    assert w.tab.model.data(idx, Qt.UserRole + 1).width() == 256
    # Stars: shown in the folder and in Starred, from what was saved, even after disconnecting.
    assert w.tab.model.columns()[-1] == ""
    w.star([servers.join(k, "/music/notes.txt")], True)
    assert w.tab.model.starred(next(x for x in w.tab.model.rows if x.name == "notes.txt"))
    w.disconnect_server(k + "/")
    w.go("starred:")
    assert [(x.name, x.size) for x in w.tab.model.rows] == [("notes.txt", len(b"server notes"))]
    assert places.star_meta()[servers.join(k, "/music/notes.txt")]["is_dir"] is False


@pytest.mark.skipif(sys.platform != "win32" or not os.environ.get("CI"), reason="Windows' network calls: CI only")
def test_real_network_calls():
    from pr7e import places, winshell
    assert isinstance(places.network_computers(), list)       # whatever the runner's network has (often nothing)
    hidden = [n for n, _r in winshell.computer_shares(r"\\localhost", hidden=True)]
    assert any(n.upper() in ("C$", "ADMIN$") for n in hidden), hidden
    assert all(not n.endswith("$") for n, _r in places.computer_shares(r"\\localhost"))


def test_side_button_reported_as_left_click(win, home, monkeypatch):
    """Qt 6 on Windows can report the mouse's back button as the LEFT button: Windows is asked which is down."""
    from PySide6.QtCore import QPointF
    from PySide6.QtGui import QMouseEvent
    from pr7e import main_window
    w = win
    w.go(str(home / "Music"))
    w.go(str(home / "Music" / "Paragon"))
    down = set()
    monkeypatch.setattr(main_window, "ASK_WINDOWS", True)
    monkeypatch.setattr(main_window, "_key_down", lambda vk: vk in down)
    view = w.tab.view.viewport()
    rect = w.tab.view.visualRect(w.tab.model.index(0, 0))
    pos = QPointF(rect.center())

    def send(kind, button, buttons):
        QApplication.sendEvent(view, QMouseEvent(kind, pos, pos, button, buttons, Qt.NoModifier))
    T = QMouseEvent.Type
    down.add(main_window.VK_XBUTTON1)                    # Windows: back down, left up
    send(T.MouseButtonPress, Qt.LeftButton, Qt.LeftButton)  # ...but Qt says "left"
    send(T.MouseMove, Qt.NoButton, Qt.LeftButton)
    down.clear()
    send(T.MouseButtonRelease, Qt.LeftButton, Qt.NoButton)
    assert w.tab.location == str(home / "Music")
    assert w.tab.selected() == [] or [e.name for e in w.tab.selected()] == ["Paragon"]  # (going up selects it)
    # Forward the same way.
    down.add(main_window.VK_XBUTTON2)
    send(T.MouseButtonPress, Qt.LeftButton, Qt.LeftButton)
    down.clear()
    send(T.MouseButtonRelease, Qt.LeftButton, Qt.NoButton)
    assert w.tab.location == str(home / "Music" / "Paragon")
    # A real left click (left down) still selects, and goes nowhere.
    w.tab.view.clearSelection()
    down.add(main_window.VK_LBUTTON)
    send(T.MouseButtonPress, Qt.LeftButton, Qt.LeftButton)
    down.clear()
    send(T.MouseButtonRelease, Qt.LeftButton, Qt.NoButton)
    assert w.tab.location == str(home / "Music" / "Paragon")
    assert [e.name for e in w.tab.selected()] == ["ELSEWORLD"]


def test_progress_shows_by_itself(win, home):
    """Files opens its operations panel for anything that isn't instant, shows a bar, then "Finished"."""
    from pr7e import ops

    class Slow(ops.Job):
        kind = "copy"

        def __init__(self):
            super().__init__()
            self.title = "Copying “big” to “Backup”"
            self.total_bytes = 1000

        def work(self):
            for _ in range(10):
                self.check()
                time.sleep(0.08)
                self.done_bytes += 100

    w = win
    w.sync_jobs = False
    job = w.run(Slow())
    assert wait(lambda: w.popover.isVisible(), 3)              # opened by itself
    assert w.progress.isVisible() and w.headerbar.ops.isVisible()
    assert wait(lambda: w.progress.value() > 0, 3)
    assert w.popover.rows[0][0].text() == job.title
    assert w.runner.wait(5)
    QApplication.processEvents()
    assert w.popover.isVisible() and w.popover.rows[0][2].text() == "Finished"   # for a moment
    assert not w.progress.isVisible()
    assert w.status.text() == "Copied “big” to “Backup”"
    assert wait(lambda: not w.popover.isVisible() and not w.headerbar.ops.isVisible(), 4)


def test_closing_the_panel_keeps_it_closed(win, home):
    from pr7e import ops

    class Slow(ops.Job):
        kind = "copy"

        def work(self):
            for _ in range(15):
                self.check()
                time.sleep(0.05)

    w = win
    w.sync_jobs = False
    w.run(Slow())
    assert wait(lambda: w.popover.isVisible(), 3)
    w._key("operations")                                        # closed by hand
    QApplication.processEvents()
    time.sleep(0.2)
    QApplication.processEvents()
    assert not w.popover.isVisible()
    assert w.runner.wait(5)


def test_instant_moves_leave_a_note(win, home):
    w = win
    w.drop([str(home / "Documents" / "notes.txt")], str(home / "Music"))   # same drive: a rename
    assert (home / "Music" / "notes.txt").exists()
    assert w.status.text() == "Moved “notes.txt” to “Music”"
    assert not w.popover.isVisible()                                         # nothing to watch


def test_properties_permissions(win, home):
    from pr7e import system
    from pr7e.dialogs import PropertiesDialog
    from pr7e import fs as fsmod
    f = home / "Documents" / "notes.txt"
    d = PropertiesDialog([fsmod.entry_for(str(f))], win)
    texts = [d.grid.itemAt(i).widget().text() for i in range(d.grid.count()) if d.grid.itemAt(i).widget()
             and hasattr(d.grid.itemAt(i).widget(), "text")]
    assert "PERMISSIONS" in texts and "OWNER" in texts and "Read and write" in texts
    assert not d.readonly.isChecked()
    d.readonly.setChecked(True)                    # changes the file at once
    assert system.is_readonly(str(f)) and d.access.text() in ("Read only", "Read and write")
    d.readonly.setChecked(False)
    assert not system.is_readonly(str(f)) and os.access(str(f), os.W_OK)
    assert d.hidden.isEnabled() == system.WINDOWS
    d.done(0)
    folder = PropertiesDialog([fsmod.entry_for(str(home / "Music"))], win)
    assert not folder.readonly.isVisibleTo(folder)  # read-only on folders means something else on Windows
    folder.done(0)


def test_open_with_and_folder_menu_items(win, home):
    from pr7e import winshell
    w = win
    menus = []
    w.answers["context"] = menus.append
    w.go(str(home / "Documents"))
    w.tab.select_paths([str(home / "Documents" / "notes.txt")])
    w._item_menu(w.tab.selected())
    texts = [a.text() for a in menus[-1].actions()]
    assert "Open With Other Application…" in texts
    w._background_menu()
    more = next(a for a in menus[-1].actions() if a.text() == "Show More Options")
    assert more.isEnabled() == winshell.available


@pytest.mark.skipif(sys.platform != "win32" or not os.environ.get("CI"), reason="Windows' shell: CI only")
def test_real_windows_folder_menu_open_with_and_hidden(tmp_path):
    from pr7e import system, winshell
    items = [i.strip() for i in winshell.background_menu_items(str(tmp_path))]
    assert "New" in items, items
    f = tmp_path / "a.txt"
    f.write_text("a")
    apps = winshell.open_with_apps(str(f))
    assert apps and all(name and handler for name, handler in apps), apps
    system.set_hidden(str(f), True)
    assert system.is_hidden_attr(str(f))
    system.set_hidden(str(f), False)
    assert not system.is_hidden_attr(str(f))
    assert system.owner(str(f))


def test_family_links(win, home, monkeypatch):
    from pr7e import family
    installed = {"PR7-W", "PR7-L", "PR7-Z"}
    started = []
    monkeypatch.setattr(family, "find", lambda app: f"C:\\{app}.exe" if app in installed else None)
    monkeypatch.setattr(family, "launch", lambda app, args: started.append((app, list(args))) or True)
    (home / "Music" / "song.mp3").write_bytes(b"x")
    (home / "Music" / "set.7z").write_bytes(b"x")
    w = win
    menus = []
    w.answers["context"] = menus.append

    def menu_for(*paths):
        w.tab.select_paths([str(p) for p in paths])
        w._item_menu(w.tab.selected())
        return {a.text(): a for a in menus[-1].actions() if a.text()}
    w.go(str(home / "Documents"))
    items = menu_for(home / "Documents" / "notes.txt")
    items["Edit in PR7-W"].trigger()
    assert started[-1] == ("PR7-W", [str(home / "Documents" / "notes.txt")])
    assert "Edit Tags in PR7-L" not in items and "Open in PR7-Z" not in items
    w.go(str(home / "Music"))
    items = menu_for(home / "Music" / "song.mp3", home / "Music" / "Paragon")
    items["Edit Tags in PR7-L"].trigger()
    assert started[-1] == ("PR7-L", [str(home / "Music" / "song.mp3"), str(home / "Music" / "Paragon")])
    items = menu_for(home / "Music" / "set.7z")
    items["Open in PR7-Z"].trigger()
    assert started[-1] == ("PR7-Z", [str(home / "Music" / "set.7z")])
    items = menu_for(home / "Music" / "song.mp3")
    assert "Play in PR7-M" not in items                # not installed
    installed.update({"PR7-R", "PR7-P", "PR7-M"})
    items = menu_for(home / "Music" / "song.mp3")
    items["Play in PR7-M"].trigger()
    assert started[-1] == ("PR7-M", [str(home / "Music" / "song.mp3")]) and "Edit Tags in PR7-L" in items
    for name in ("a.pdf", "b.pdf", "pic.jpg", "clip.mkv"):
        (home / "Music" / name).write_bytes(b"x")
    w.go(str(home / "Music"))
    items = menu_for(home / "Music" / "a.pdf", home / "Music" / "b.pdf")
    items["Open in PR7-R"].trigger()
    assert started[-1] == ("PR7-R", [str(home / "Music" / "a.pdf"), str(home / "Music" / "b.pdf")])
    items = menu_for(home / "Music" / "pic.jpg")
    items["View in PR7-P"].trigger()
    assert started[-1] == ("PR7-P", [str(home / "Music" / "pic.jpg")]) and "Play in PR7-M" not in items
    items = menu_for(home / "Music" / "clip.mkv")
    assert "Play in PR7-M" in items and "Open in PR7-R" not in items
    items = menu_for(home / "Music" / "pic.jpg", home / "Music" / "clip.mkv")
    assert "View in PR7-P" not in items and "Play in PR7-M" not in items      # one file each
    folder = home / "Music" / "Paragon"
    assert "Search with PR7-IS" not in menu_for(folder)          # PR7-IS not installed
    installed.add("PR7-IS")
    menu_for(folder)["Search with PR7-IS"].trigger()             # a folder: PR7-IS searching inside it
    assert started[-1] == ("PR7-IS", ['"' + str(folder) + os.sep + '"'])
    assert "Search with PR7-IS" not in menu_for(folder, home / "Music" / "song.mp3")   # one folder only
    w._background_menu()                                          # empty space: the folder shown
    {a.text(): a for a in menus[-1].actions()}["Search with PR7-IS"].trigger()
    assert started[-1] == ("PR7-IS", ['"' + str(home / "Music") + os.sep + '"'])
    installed.clear()                                  # not installed: not offered
    items = menu_for(home / "Music" / "song.mp3")
    assert "Edit Tags in PR7-L" not in items


def test_family_lookup_is_windows_only():
    from pr7e import family
    if sys.platform != "win32":
        assert family.find("PR7-W") is None and not family.pr7e_for_folders()
    assert family.find("not-a-family-app") is None
