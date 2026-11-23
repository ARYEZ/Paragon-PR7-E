"""PR7-E's window, working the way GNOME Files works: the sidebar of places; the header bar (back, forward, the
path bar, search, the view switch, the operations key, the main menu); tabs; the folder in grid or list view with
the floating selection label. Files' menus, keys, clipboard, drag and drop, Undo / Redo and Trash.

Anything that asks (rename, replace, delete for good...) goes through `_ask`, which tests replace with `answers`.
"""

from __future__ import annotations

import os
import posixpath
import struct
import threading

from PySide6.QtCore import QFileSystemWatcher, QItemSelectionModel, QMimeData, QObject, QPoint, Qt, QTimer, QUrl, Signal
from PySide6.QtGui import QAction, QGuiApplication, QKeySequence
from PySide6.QtWidgets import (QApplication, QFileDialog, QHBoxLayout, QMainWindow, QMenu, QStackedWidget,
                               QVBoxLayout, QWidget)

from . import archive, fs, ops, places, remote, rename, servers, settings, system, theme, trash, winshell
from .views import (local_paths, ConnectBar, EmptyState, EntryModel, FloatingStatus, GridView, HeaderBar, ListView, OperationsPopover,
                    SearchFilters, Sidebar, TabStrip, TrashBar)
from .widgets import Header
from .workers import Asker, JobRunner, Loader, Search

WINDOWS_EFFECT = 'application/x-qt-windows-mime;value="Preferred DropEffect"'
GNOME_COPIED = "x-special/gnome-copied-files"
WINDOWS: list["MainWindow"] = []    # open windows (kept alive here)
REMOTE_MIME = "application/x-pr7e-remote"   # server items on the clipboard / in a drag: their addresses


def key(loc: str) -> str:
    """A location for comparing: case-folded folder paths (Windows), server addresses without a trailing /."""
    return loc.rstrip("/") if servers.is_remote(loc) else os.path.normcase(loc)


def parent_of(loc: str) -> str:
    if servers.is_remote(loc):
        k, path = servers.split(loc)
        return servers.join(k, posixpath.dirname(path.rstrip("/")) or "/")
    return os.path.dirname(loc)


def _login_refused(error) -> bool:
    t = str(error).lower()
    return "530" in t or "login" in t or "authentication" in t or "password" in t


class _Bridge(QObject):
    done = Signal(object)


def pretty(path: str) -> str:
    """A folder as Files shows it in LOCATION: under Home as "Home/Music/...", else the full path."""
    home = places.home()
    if fs.is_inside(path, home):
        rel = os.path.relpath(path, home)
        return "Home" if rel == "." else "Home/" + rel.replace("\\", "/")
    return path


class Tab(QWidget):
    """One tab: its location, its back / forward history, its own model and views (each tab keeps its
    scroll position and selection), and its search."""

    def __init__(self, window: "MainWindow", location: str):
        super().__init__()
        self.window_ = window
        self.location = location
        self.back: list[str] = []
        self.forward: list[str] = []
        self.model = EntryModel(window.loader, self)
        self.grid = GridView()
        self.list = ListView()
        self.stack = QStackedWidget()
        for v in (self.grid, self.list):
            v.setModel(self.model)
            self.stack.addWidget(v)
        lay = QVBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self.stack)
        self.search: dict | None = None        # {"text", "when", "what", "root", "generation", "done"}
        self.loading = False                    # a server folder being listed
        self.pending_select: list | None = None  # what to select once a server listing arrives
        self.expanded: set[str] = set()         # list view: folders opened in place (normcase paths)
        self.error = ""

    @property
    def view(self):
        return self.stack.currentWidget()

    def title(self) -> tuple[str, str]:
        """(icon, title) for the tab bar and the window title."""
        if self.search is not None and self.search.get("text"):
            return "search", f"Search for “{self.search['text']}”"
        if self.location in places.SPECIAL:
            icon = {"recent:": "recent", "starred:": "star", "trash:": "trash", "other:": "network", "network:": "network"}[self.location]
            return icon, places.SPECIAL[self.location]
        if servers.is_remote(self.location):
            srv = servers.server_for(self.location)
            path = servers.split(self.location)[1]
            name = posixpath.basename(path.rstrip("/"))
            return ("server", name) if name else ("server", srv.label if srv else self.location)
        if places.is_computer(self.location):
            return "desktop", self.location.lstrip("\\")
        if self.location.startswith("\\\\") and \
                os.path.normcase(places.share_root(self.location)) == os.path.normcase(self.location.rstrip("\\")):
            return "share", places.share_label(self.location)
        if os.path.normcase(self.location) == os.path.normcase(places.home()):
            return "home", "Home"
        d = places.drive_of(self.location, self.window_.drives)
        if d and os.path.normcase(d.root) == os.path.normcase(self.location):
            return "usb" if d.removable else "drive", d.label
        return "folder", os.path.basename(os.path.normpath(self.location)) or self.location

    def selected(self) -> list[fs.Entry]:
        rows = sorted({i.row() for i in self.view.selectionModel().selectedIndexes()})
        return [self.model.rows[r] for r in rows if r < len(self.model.rows)]

    def select_paths(self, paths, current: bool = True) -> None:
        sm = self.view.selectionModel()
        sm.clearSelection()
        first = None
        for p in paths:
            r = self.model.row_of(p)
            if r >= 0:
                idx = self.model.index(r, 0)
                sm.select(idx, QItemSelectionModel.Select | QItemSelectionModel.Rows)
                first = first or idx
        if first is not None and current:
            sm.setCurrentIndex(first, QItemSelectionModel.NoUpdate)
            self.view.scrollTo(first)


class MainWindow(QMainWindow):
    def __init__(self, folders: list[str] | None = None, parent=None):
        super().__init__(parent)
        self.setWindowTitle("PR7-E")
        self.resize(1280, 800)
        self.answers: dict = {}                 # tests: kind -> function(payload) instead of a dialog
        self.shell_reg = None                   # tests: a stand-in registry for "Use PR7-E for Folders"
        self._bridge = _Bridge()
        self._bridge.done.connect(lambda box: box[0](*box[1]), Qt.QueuedConnection)
        self.asker = Asker(self._ask)
        self.runner = JobRunner(self.asker)
        self.runner.finished.connect(self._job_done)
        self.sync_jobs = False                  # tests: run file operations here and now
        self.loader = Loader()
        self.loader.thumb_ready.connect(self._thumb_ready)
        self.loader.count_ready.connect(self._count_ready)
        self.searcher = Search()
        # Queued: results are matched to the search that asked, which is recorded once start() returns.
        self.searcher.found.connect(self._found, Qt.QueuedConnection)
        self.searcher.done.connect(self._search_done, Qt.QueuedConnection)
        self.undo_stack: list[ops.Job] = []
        self.redo_stack: list[ops.Job] = []
        self.drives = places.drives()
        self.show_hidden = settings.get_bool("show_hidden")
        self.sort = settings.get("sort", "name")
        self.view_mode = settings.get("view", "grid")
        self.zoom = min(len(GridView.ZOOMS) - 1, max(0, settings.get_int("zoom", 2)))
        self.prefs = load_prefs()
        self.watcher = QFileSystemWatcher(self)
        self.watcher.directoryChanged.connect(self._dir_changed)
        self.reload_timer = QTimer(self, singleShot=True, interval=250)
        self.reload_timer.timeout.connect(self._reload_dirty)
        self.dirty: set[str] = set()
        self.search_timer = QTimer(self, singleShot=True, interval=250)
        self.search_timer.timeout.connect(self._run_search)
        self.tick = QTimer(self, interval=150)
        self.tick.timeout.connect(self._tick)
        self.drive_timer = QTimer(self, interval=3000)
        self.drive_timer.timeout.connect(self._check_drives)
        self.drive_timer.start()

        root = QWidget()
        v = QVBoxLayout(root)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(0)
        self.header = Header("FILES")
        v.addWidget(self.header)
        body = QHBoxLayout()
        body.setSpacing(0)
        self.sidebar = Sidebar()
        self.sidebar.activated_place.connect(self._place)
        self.sidebar.dropped.connect(self._sidebar_drop)
        self.sidebar.eject.connect(lambda pl: self.disconnect_server(pl.path) if pl.kind == "server" else
                                   self.forget_share(pl.path) if pl.kind == "share" else self._eject(pl.path))
        self.sidebar.customContextMenuRequested.connect(self._sidebar_menu)
        self.sidebar.setVisible(settings.get_bool("sidebar", True))
        body.addWidget(self.sidebar)
        right = QVBoxLayout()
        right.setSpacing(0)
        self.headerbar = HeaderBar()
        self.headerbar.triggered.connect(self._key)
        self.headerbar.path.go.connect(lambda loc: self.go(loc))
        self.headerbar.path.edit.connect(self.edit_location)
        self.headerbar.path.menu.connect(lambda: self._background_menu(
            self.headerbar.path.mapToGlobal(QPoint(self.headerbar.path.width() - 30, self.headerbar.path.height()))))
        self.headerbar.location.returnPressed.connect(self._location_entered)
        self.headerbar.location.installEventFilter(self)
        self.headerbar.search.textEdited.connect(lambda _t: self.search_timer.start())
        self.headerbar.search.installEventFilter(self)
        right.addWidget(self.headerbar)
        self.tabbar = TabStrip()
        self.tabbar.current_changed.connect(self.set_current_tab)
        self.tabbar.close_requested.connect(self.close_tab)
        self.tabbar.new_requested.connect(lambda: self.new_tab())
        right.addWidget(self.tabbar)
        self.filters = SearchFilters()
        self.filters.changed.connect(self._run_search)
        right.addWidget(self.filters)
        self.trashbar = TrashBar()
        self.trashbar.restore.connect(lambda: self.restore(self.tab.selected() or self.tab.model.rows))
        self.trashbar.empty.connect(self.empty_trash)
        right.addWidget(self.trashbar)
        self.content = QStackedWidget()
        right.addWidget(self.content, 1)
        self.connectbar = ConnectBar()
        self.connectbar.connect_to.connect(lambda a: self.connect_server(a))
        right.addWidget(self.connectbar)
        body.addLayout(right, 1)
        v.addLayout(body, 1)
        self.setCentralWidget(root)
        self.status = FloatingStatus(self.content)
        self.empty = EmptyState(self.content)
        self.popover = OperationsPopover(self)
        self.tabs: list[Tab] = []
        self.fill_sidebar()
        self._actions()
        for f in folders or [places.home()]:
            self.new_tab(f)
        self.set_current_tab(0)
        geo = settings.get("geometry")
        if geo:
            try:
                self.restoreGeometry(bytes.fromhex(geo))
            except ValueError:
                pass
        WINDOWS.append(self)
        # The mouse's side buttons (back / forward) arrive as clicks on whatever is under the pointer, which the
        # views would swallow: watch the whole application and act when the click is inside this window.
        QApplication.instance().installEventFilter(self)

    # ================================================================== tabs
    @property
    def tab(self) -> Tab:
        return self.tabs[self.content.currentIndex()] if self.tabs else None

    def new_tab(self, location: str | None = None, switch: bool = True) -> Tab:
        location = location or (self.tab.location if self.tab and self.tab.location else places.home())
        t = Tab(self, location)
        for view in (t.grid, t.list):
            view.activated.connect(lambda _i, t=t: None if self.prefs["click"] == "single" else self.open_selected(t))
            view.clicked.connect(lambda i, t=t: self._single_click(t, i))
            view.customContextMenuRequested.connect(lambda pos, t=t, view=view: self._view_menu(t, view, pos))
            view.selectionModel().selectionChanged.connect(lambda *_a, t=t: self._selection(t))
            view.dropped.connect(lambda paths, target, action, t=t: self.drop(paths, target or t.location, action))
            view.typed.connect(self.start_search)
        t.list.star_clicked.connect(lambda e: self.star([e.path], not t.model.starred(e)))
        t.list.sort_clicked.connect(self._sort_column)
        t.list.expand.connect(lambda e, on, t=t: self.expand_folder(t, e, on))
        t.model.thumbs_ok = lambda e: self._allowed("thumbnails", e.path)
        t.grid.set_icon_size(GridView.ZOOMS[self.zoom])
        t.stack.setCurrentWidget(t.grid if self.view_mode == "grid" else t.list)
        index = self.content.currentIndex() + 1 if self.tabs else 0
        self.tabs.insert(index, t)
        self.content.insertWidget(index, t)
        self.load(t)
        if switch:
            self.set_current_tab(index)
        else:
            self._tabbar()
        return t

    def set_current_tab(self, i: int) -> None:
        if not 0 <= i < len(self.tabs):
            return
        self.content.setCurrentIndex(i)
        self._tabbar()
        self._chrome()
        self.tab.view.setFocus()

    def close_tab(self, i: int | None = None) -> None:
        i = self.content.currentIndex() if i is None else i
        if len(self.tabs) <= 1:
            self.close()
            return
        t = self.tabs.pop(i)
        self.content.removeWidget(t)
        t.deleteLater()
        self.set_current_tab(min(i, len(self.tabs) - 1))
        self._watch()

    def _tabbar(self):
        self.tabbar.set_tabs([t.title() for t in self.tabs], self.content.currentIndex())

    # ================================================================== locations
    def go(self, location: str, tab: Tab | None = None, record: bool = True) -> None:
        """Go to a folder or a place (recent: starred: trash: other:)."""
        tab = tab or self.tab
        if servers.is_remote(location):
            if servers.server_for(location) is None:
                self.connect_server(location, tab=tab)
                return
        elif location in places.SPECIAL or places.is_computer(location):
            pass  # Network, or a computer on it: listed when loaded
        elif places.share_path(location) and not os.path.isdir(location):
            self.open_share(places.share_path(location), tab)  # log in first
            return
        else:
            location = os.path.abspath(location)
            if not os.path.isdir(location):
                self._message("Unable to access location", f"“{location}” can’t be found.", [])
                return
        if tab.search is not None:
            self.end_search(tab, reload=False)
        if record and key(location) != key(tab.location):
            tab.back.append(tab.location)
            tab.forward.clear()
        old = tab.location
        tab.location = location
        self.load(tab, keep_selection=False)
        # Going up selects the folder you came from, as Files does.
        if old and old not in places.SPECIAL and key(parent_of(old)) == key(location):
            tab.select_paths([old])  # (a server folder: once its listing arrives)
            tab.pending_select = [old]

    def back(self):
        t = self.tab
        if t.search is not None:
            self.end_search()
            return
        if t.back:
            t.forward.append(t.location)
            self.go(t.back.pop(), record=False)

    def forward(self):
        t = self.tab
        if t.forward:
            t.back.append(t.location)
            self.go(t.forward.pop(), record=False)

    def up(self):
        loc = self.tab.location
        if loc in places.SPECIAL:
            return
        if places.is_computer(loc):
            self.go("network:")
            return
        if loc.startswith("\\\\") and key(places.share_root(loc)) == key(loc.rstrip("\\")):
            self.go("\\\\" + loc.lstrip("\\").split("\\")[0])  # a share's computer
            return
        parent = parent_of(loc) if servers.is_remote(loc) else os.path.dirname(os.path.normpath(loc))
        if parent and key(parent) != key(loc):
            self.go(parent)

    def reload(self):
        if self.tab.search is not None:
            self._run_search()
        else:
            self.load(self.tab)

    def load(self, tab: Tab, keep_selection: bool = True) -> None:
        """Read the tab's location into its model."""
        keep = [e.path for e in tab.selected()] if keep_selection and tab.model.rows else []
        scroll = tab.view.verticalScrollBar().value() if keep_selection else 0
        loc = tab.location
        tab.error = ""
        tab.loading = False
        if servers.is_remote(loc):
            self._load_remote(tab, keep, scroll)
            return
        if loc == "network:" or places.is_computer(loc):
            self._load_network(tab)
            return
        mode, entries = "folder", []
        try:
            if loc == "recent:":
                mode = "recent"
                for path, _when in places.recent():
                    try:
                        e = fs.entry_for(path)
                    except OSError:
                        continue
                    e.location = pretty(os.path.dirname(path))
                    entries.append(e)
            elif loc == "starred:":
                mode = "starred"
                meta = places.star_meta()
                for path in places.starred():
                    if servers.is_remote(path):  # a server item: shown from what was saved with its star
                        m = meta.get(path, {})
                        k, rpath = servers.split(path)
                        e = fs.Entry(posixpath.basename(rpath.rstrip("/")) or path, path, bool(m.get("is_dir")),
                                     int(m.get("size", 0)), float(m.get("mtime", 0)))
                        e.location = k.split("://", 1)[1].split("@")[-1] + (posixpath.dirname(rpath.rstrip("/"))
                                                                            if rpath.count("/") > 1 else "")
                        entries.append(e)
                        continue
                    try:
                        e = fs.entry_for(path)
                    except OSError:
                        continue
                    e.location = pretty(os.path.dirname(path))
                    entries.append(e)
                entries = self._sorted(entries)
            elif loc == "trash:":
                mode = "trash"
                for it in trash.items():
                    e = fs.Entry(it.name, it.data, it.is_dir, it.size, it.deleted, location=pretty(
                        os.path.dirname(it.original)), trash=it)
                    entries.append(e)
                entries = self._sorted(entries, self.sort if self.sort != "name" else "mtime_desc")
            elif loc == "other:":
                mode = "other"
                self.drives = places.drives()
                for d in self.drives:
                    extra = f"{fs.human_size(d.free)} free of {fs.human_size(d.total)}" if d.total else ""
                    entries.append(fs.Entry(d.label, d.root, True, 0, 0, kind="usb" if d.removable else "drive",
                                            extra=extra))
                entries.append(fs.Entry("Network", "network:", True, kind="network",
                                        extra="Computers and shared folders on this network"))
            else:
                entries = self._with_children(self._sorted(fs.list_dir(loc, self.show_hidden)), tab)
        except OSError as e:
            tab.error = e.strerror or str(e)
        tab.model.stars = {os.path.normcase(p) for p in places.starred()}
        tab.model.expandable = mode == "folder" and self._expanding()
        tab.model.expanded = tab.expanded if tab.model.expandable else set()
        tab.model.set_rows(entries, mode)
        if mode == "folder":
            for e in entries:
                if e.is_dir:
                    self._count(e.path)
        elif mode in ("recent", "starred"):
            for e in entries:
                if e.is_dir:
                    self._count(e.path)
        if keep:
            tab.select_paths(keep, current=False)
        tab.view.verticalScrollBar().setValue(scroll)
        self._watch()
        if tab is self.tab:
            self._chrome()
        self._tabbar()

    def _load_network(self, tab: Tab, retried: bool = False) -> None:
        """Files' Networks: the computers Windows sees (Explorer's Network page), or one computer's shared folders.
        Both are asked on a worker thread: Windows can take a few seconds to look."""
        loc = tab.location
        tab.model.set_rows([], "network")
        tab.loading = True
        self._chrome()
        self._tabbar()

        def find():
            if loc == "network:":
                return [fs.Entry(name, path, True, kind="computer") for name, path in places.network_computers()]
            return [fs.Entry(name, loc + "\\" + name, True, kind="share", extra=remark)
                    for name, remark in places.computer_shares(loc)]

        def done(entries, error):
            if tab.location != loc or tab not in self.tabs:
                return
            tab.loading = False
            if error is not None and places.is_computer(loc) and getattr(error, "errno", 0) in (5, 1326) and \
                    not retried:
                # The computer wants a login before it shows its shares: as Windows does, through IPC$.
                got = self._ask("login", {"host": loc.lstrip("\\"), "user": "", "protocol": "smb"})
                if got:
                    def login_then_list():
                        places.connect_share(loc + "\\IPC$", got["user"], got["password"],
                                             got.get("remember", False))
                    self._background(login_then_list, lambda _r, err: self._load_network(tab, retried=True)
                                     if err is None else (setattr(tab, "error", str(err.strerror if isinstance(
                                         err, OSError) else err)), self._chrome()))
                    return
            if error is not None:
                tab.error = (error.strerror if isinstance(error, OSError) and error.strerror else str(error))
            tab.model.set_rows(entries or [], "network")
            if tab is self.tab:
                self._chrome()
            self._tabbar()
        self._background(find, done)

    def _load_remote(self, tab: Tab, keep, scroll) -> None:
        """A server folder: listed on a worker thread (servers can be slow); "Loading…" meanwhile."""
        loc = tab.location
        srv = servers.server_for(loc)
        tab.model.set_rows([], "remote")
        if srv is None:
            tab.error = "The server isn\u2019t connected."
            self._chrome()
            return
        tab.loading = True
        self._chrome()
        self._tabbar()

        def done(entries, error):
            if tab.location != loc or tab not in self.tabs:
                return
            tab.loading = False
            if error is not None:
                tab.error = str(error)
                tab.model.set_rows([], "remote")
            else:
                shown = [e for e in entries if self.show_hidden or not e.hidden]
                tab.model.stars = {os.path.normcase(p) for p in places.starred()}
                tab.model.set_rows(self._sorted(shown), "remote")
                for e in shown:
                    if e.is_dir:
                        self._count(e.path)
                select = getattr(tab, "pending_select", None) or keep
                tab.pending_select = None
                if select:
                    tab.select_paths(select, current=not keep)
                tab.view.verticalScrollBar().setValue(scroll)
            if tab is self.tab:
                self._chrome()
            self._tabbar()
        self._background(lambda: srv.list(servers.split(loc)[1]), done)

    def _background(self, fn, done) -> None:
        """fn() on a worker thread; done(result, error) back on the GUI thread (tests: here and now)."""
        if self.sync_jobs:
            try:
                r = fn()
            except Exception as e:  # noqa: BLE001 - shown to the user
                done(None, e)
                return
            done(r, None)
            return

        def go():
            try:
                box = (done, (fn(), None))
            except Exception as e:  # noqa: BLE001
                box = (done, (None, e))
            self._bridge.done.emit(box)
        threading.Thread(target=go, daemon=True).start()

    # ================================================================== Connect to Server
    def connect_server(self, address: str, tab: Tab | None = None) -> None:
        """Files' Connect to Server: ask for the password if it isn't remembered, connect on a worker thread
        (host key and certificate questions come to the GUI), then show the folder."""
        tab = tab or self.tab
        unc = places.share_path(address)
        if unc:
            self.open_share(unc, tab)
            return
        try:
            site, path = servers.parse(address)
        except remote.RemoteError as e:
            self._message("Unable to connect", str(e), [])
            return
        k = servers.key_for(site)
        if k in servers.CONNECTED:
            srv = servers.CONNECTED[k]
            self.go(servers.join(k, path if path != "/" else srv.home), tab=tab)
            return
        remember = False
        if not site.password:
            site.password = servers.saved_password(k) or ""
        if site.user == "anonymous-refused":  # anonymous was tried and refused: now ask
            site.user = ""
            k = servers.key_for(site)
        if not site.password and not (site.protocol == "ftp" and site.logon == "anonymous" and "@" not in address):
            got = self._ask("login", {"host": site.host, "user": "" if site.user == "anonymous" else site.user,
                                      "protocol": site.protocol})
            if not got:
                return
            if got.get("anonymous"):
                site.user, site.password, site.logon = "anonymous", "", "anonymous"
            else:
                site.user, site.password, site.logon = got["user"], got["password"], "normal"
                remember = got.get("remember", False)
        self.empty.show_state("server", "Connecting\u2026", site.host)

        def asks_hostkey(*a):
            return self.asker.ask("hostkey", a)

        def asks_cert(host, info):
            return self.asker.ask("cert", (host, info))

        def done(srv, error):
            if error is not None and site.logon == "anonymous" and _login_refused(error):
                # Anonymous wasn't allowed: ask for a name and password, as Files does.
                user = address.split("://", 1)[-1]
                self.connect_server(address.replace("://", "://anonymous-refused@", 1) if "@" not in user
                                    else address, tab=tab)
                return
            if error is not None:
                self._empty_state()
                self._message("Unable to connect", f"PR7-E couldn\u2019t connect to \u201c{site.host}\u201d.",
                              [str(error)])
                return
            if remember:
                servers.save_password(srv.key, site.password)
            servers.add_recent(srv.key)
            self.fill_sidebar_all()
            self.go(servers.join(srv.key, path if path != "/" else srv.home), tab=tab)
        self._background(lambda: servers.connect(site, hostkey=asks_hostkey, cert=asks_cert), done)

    def open_share(self, unc: str, tab: Tab | None = None) -> None:
        """A Windows network share (\\\\server\\share): opened as it is when Windows already has access; else Files'
        login question, then Windows' own connection (WNetAddConnection2W, as Map network drive)."""
        tab = tab or self.tab

        def opened():
            places.add_share(unc)
            self.fill_sidebar_all()
            self.go(unc, tab=tab)
        if os.path.isdir(unc):
            opened()
            return
        got = self._ask("login", {"host": unc.lstrip("\\").split("\\")[0], "user": "", "protocol": "smb"})
        if not got:
            return
        self.empty.show_state("share", "Connecting\u2026", unc)

        def done(_r, error):
            if error is None and os.path.isdir(unc):
                opened()
                return
            self._empty_state()
            reason = (error.strerror or str(error)) if isinstance(error, OSError) else str(error or "")
            self._message("Unable to connect", f"PR7-E couldn\u2019t open \u201c{unc}\u201d.",
                          [reason or "The share can\u2019t be found."])
        self._background(lambda: places.connect_share(unc, got["user"], got["password"], got.get("remember", False)),
                         done)

    def forget_share(self, path: str) -> None:
        """The sidebar's eject on a share: off the sidebar, and Windows' connection to it closed."""
        places.remove_share(path)
        places.disconnect_share(path)
        self.fill_sidebar_all()

    def disconnect_server(self, k: str) -> None:
        """The sidebar's eject on a server: tabs showing it go to Other Locations."""
        servers.disconnect(k.rstrip("/"))
        for w in WINDOWS:
            for t in w.tabs:
                if servers.is_remote(t.location) and servers.split(t.location)[0] == k.rstrip("/"):
                    w.go("other:", tab=t)
            w.fill_sidebar()

    def _chrome(self) -> None:
        """Everything outside the view that follows the current tab."""
        t = self.tab
        if t is None:
            return
        icon, title = t.title()
        self.setWindowTitle(title)
        hb = self.headerbar
        hb.keys["back"].setEnabled(bool(t.back) or t.search is not None)
        hb.keys["forward"].setEnabled(bool(t.forward))
        hb.path.set_segments(self._segments(t.location))
        if t.search is not None:
            hb.show_mode("search")
            if hb.search.text() != t.search["text"]:
                hb.search.setText(t.search["text"])
            self.filters.when.set_value(t.search["when"])
            self.filters.what.set_value(t.search["what"])
            self.filters.where.setText(f"Searching {self._root_name(t.search['root'])} and the folders in it")
            hb.search.setPlaceholderText(f"Search everything in {self._root_name(t.search['root'])}")
        elif hb.location.isVisible():
            pass
        else:
            hb.show_mode("path")
        self.filters.setVisible(t.search is not None)
        self.trashbar.setVisible(t.location == "trash:" and t.search is None)
        self.trashbar.empty_btn.setEnabled(bool(t.model.rows))
        hb.keys["view"].icon_name = "list" if self.view_mode == "grid" else "grid"
        hb.keys["view"].setToolTip("List view (Ctrl+2)" if self.view_mode == "grid" else "Grid view (Ctrl+1)")
        hb.keys["view"].update()
        side = t.location
        if servers.is_remote(side):
            side = servers.split(side)[0] + "/"
        elif side.startswith("\\\\"):
            side = places.share_root(side)
        self.sidebar.select_path(side if t.search is None else "")
        for view in (t.grid, t.list):
            view.can_drop = t.location not in places.SPECIAL and t.search is None and not places.is_computer(t.location)
        self.connectbar.setVisible(t.location == "other:" and t.search is None)
        self._empty_state()
        self._selection(t)

    def _root_name(self, root: str) -> str:
        if servers.is_remote(root):
            name = posixpath.basename(servers.split(root)[1].rstrip("/"))
            srv = servers.server_for(root)
            return name or (srv.label if srv else root)
        if os.path.normcase(root) == os.path.normcase(places.home()):
            return "Home"
        return os.path.basename(os.path.normpath(root)) or root

    def _segments(self, loc: str) -> list[tuple[str, str, str]]:
        """The path bar's buttons: from Home (or the drive) down to the folder."""
        if loc in places.SPECIAL:
            icon = {"recent:": "recent", "starred:": "star", "trash:": "trash", "other:": "network", "network:": "network"}[loc]
            return [(icon, places.SPECIAL[loc], loc)]
        if servers.is_remote(loc):
            k, path = servers.split(loc)
            srv = servers.server_for(loc)
            segs = [("server", srv.label if srv else k, k + "/")]
            cur = "/"
            for part in [x for x in path.split("/") if x]:
                cur = posixpath.join(cur, part)
                segs.append(("", part, servers.join(k, cur)))
            return segs
        home = places.home()
        if places.is_computer(loc):
            return [("network", "Network", "network:"), ("desktop", loc.lstrip("\\"), loc)]
        if loc.startswith("\\\\"):
            base = places.share_root(loc)
            computer, share = base.lstrip("\\").split("\\")[:2]
            segs = [("desktop", computer, "\\\\" + computer), ("share", share, base)]
        elif fs.is_inside(loc, home):
            segs = [("home", "Home", home)]
            base = home
        else:
            d = places.drive_of(loc, self.drives)
            base = d.root if d else os.path.splitdrive(loc)[0] + os.sep if os.name == "nt" else "/"
            label = d.label if d else base
            segs = [("usb" if d and d.removable else "drive", label, base)]
        rel = os.path.relpath(loc, base)
        if rel != ".":
            cur = base
            for part in rel.split(os.sep):
                cur = os.path.join(cur, part)
                segs.append(("", part, cur))
        return segs

    def _empty_state(self):
        t = self.tab
        if t.error:
            self.empty.show_state("folder", "Unable to Access Location", t.error)
        elif getattr(t, "loading", False):
            self.empty.show_state("server", "Loading\u2026")
        elif t.model.rows:
            self.empty.show_state("", "")
        elif t.search is not None:
            busy = not t.search.get("done", True)
            self.empty.show_state("search", "" if busy or not (t.search["text"] or t.search["when"] or
                                                               t.search["what"]) else "No Results Found",
                                  "Try a different search.")
        elif t.location == "trash:":
            self.empty.show_state("trash", "Trash is Empty")
        elif t.location == "starred:":
            self.empty.show_state("star", "No Starred Files", "Star an item with its right-click menu.")
        elif t.location == "recent:":
            self.empty.show_state("recent", "No Recent Files")
        elif t.location == "network:":
            self.empty.show_state("network", "No Computers Found", "Windows hasn\u2019t found any computers on this "
                                  "network. Type a share\u2019s address (\\\\pc\\share) in Connect to Server.")
        elif places.is_computer(t.location):
            self.empty.show_state("desktop", "No Shared Folders", "This computer doesn\u2019t share any folders.")
        else:
            self.empty.show_state("folder", "Folder is Empty")

    def _place(self, pl, new_tab: bool):
        if new_tab:
            self.new_tab(pl.path)
        else:
            self.go(pl.path)

    def edit_location(self):
        """Ctrl+L: the path bar becomes a text field holding the path, selected (a server address works too)."""
        t = self.tab
        hb = self.headerbar
        hb.location.setText("" if t.location in places.SPECIAL else t.location)
        hb.show_mode("location")
        hb.location.setFocus()
        hb.location.selectAll()

    def _location_entered(self):
        text = os.path.expandvars(os.path.expanduser(self.headerbar.location.text().strip()))
        self.headerbar.show_mode("path")
        if not text:
            return
        if servers.is_remote(text):
            self.go(text)
            return
        if places.share_path(text) and not os.path.isdir(places.share_path(text)):
            self.open_share(places.share_path(text))  # a share Windows can't open yet: log in
            return
        if text.lower().startswith("file:"):
            text = os.path.normpath(QUrl(text).toLocalFile())
        if os.path.isfile(text):
            self.go(os.path.dirname(text))
            self.tab.select_paths([text])
            return
        self.go(text)

    def eventFilter(self, obj, e):
        if e.type() == e.Type.MouseButtonPress and e.button() in (Qt.BackButton, Qt.ForwardButton) and \
                isinstance(obj, QWidget) and obj.window() is self:
            self.back() if e.button() == Qt.BackButton else self.forward()
            return True
        if e.type() == e.Type.MouseButtonRelease and e.button() in (Qt.BackButton, Qt.ForwardButton) and \
                isinstance(obj, QWidget) and obj.window() is self:
            return True  # the release too, so nothing else acts on it
        if e.type() == e.Type.KeyPress and e.key() == Qt.Key_Escape:
            if obj is self.headerbar.location:
                self.headerbar.show_mode("path")
                self.tab.view.setFocus()
                return True
            if obj is self.headerbar.search:
                self.end_search()
                return True
        if e.type() == e.Type.KeyPress and obj is self.headerbar.search and e.key() in (Qt.Key_Down, Qt.Key_Return,
                                                                                          Qt.Key_Enter):
            self.tab.view.setFocus()
            if not self.tab.selected() and self.tab.model.rows:
                self.tab.select_paths([self.tab.model.rows[0].path])
            return True
        if e.type() == e.Type.FocusOut and obj is self.headerbar.location:
            QTimer.singleShot(0, lambda: self.headerbar.show_mode("path") if not self.headerbar.location.hasFocus()
                              and self.headerbar.location.isVisible() else None)
        return super().eventFilter(obj, e)

    # ================================================================== search
    def start_search(self, text: str = "") -> None:
        """Ctrl+F, the search key, or typing in a folder: search it and every folder in it."""
        t = self.tab
        if t.search is None:
            root = t.location if t.location not in places.SPECIAL else places.home()
            t.search = {"text": "", "when": "", "what": "", "root": root, "generation": 0, "done": True}
        hb = self.headerbar
        hb.show_mode("search")
        if text:
            t.search["text"] = hb.search.text() + text if hb.search.hasFocus() else text
            hb.search.setText(t.search["text"])
            self.search_timer.start()
        hb.search.setFocus()
        hb.search.end(False)
        self._chrome()

    def end_search(self, tab: Tab | None = None, reload: bool = True) -> None:
        tab = tab or self.tab
        if tab.search is None:
            return
        self.searcher.stop()
        tab.search = None
        self.headerbar.search.clear()
        self.headerbar.show_mode("path")
        if reload:
            self.load(tab, keep_selection=False)
            tab.view.setFocus()

    def _run_search(self, sync: bool = False):
        t = self.tab
        if t.search is None:
            return
        s = t.search
        s["text"] = self.headerbar.search.text().strip()
        s["when"], s["what"] = self.filters.when.value, self.filters.what.value
        if not (s["text"] or s["when"] or s["what"]):
            self.searcher.stop()
            s["done"] = True
            # Nothing to look for: show the folder, as Files does.
            loc = s["root"]
            if servers.is_remote(loc):
                t.model.set_rows([], "remote")
                srv = servers.server_for(loc)
                self._background(lambda: srv.list(servers.split(loc)[1]) if srv else [],
                                 lambda es, err: (t.model.set_rows(self._sorted(
                                     [e for e in es or [] if self.show_hidden or not e.hidden]), "remote"),
                                     self._chrome()) if t.search is not None and not t.search["text"] else None)
                self._chrome()
                return
            try:
                entries = self._sorted(fs.list_dir(loc, self.show_hidden))
            except OSError:
                entries = []
            t.model.set_rows(entries, "folder")
            self._chrome()
            self._tabbar()
            return
        srv = servers.server_for(s["root"])
        t.model.set_rows([], "remote_search" if srv else "search")
        s["done"] = False
        s["generation"] = self.searcher.start(servers.split(s["root"])[1] if srv else s["root"], s["text"],
                                              s["when"], s["what"], self.show_hidden, sync=sync or self.sync_jobs,
                                              recursive=self.prefs["search_subfolders"], server=srv)
        self._chrome()
        self._tabbar()

    def _found(self, gen, batch):
        for t in self.tabs:
            if t.search is not None and t.search.get("generation") == gen:
                t.model.append(batch)
                for e in batch:
                    if e.is_dir and not servers.is_remote(e.path):
                        self._count(e.path)
                if t is self.tab:
                    self._empty_state()

    def _search_done(self, gen):
        for t in self.tabs:
            if t.search is not None and t.search.get("generation") == gen:
                t.search["done"] = True
                if t is self.tab:
                    self._empty_state()

    # ================================================================== opening
    def open_selected(self, tab: Tab | None = None, new_tab: bool = False) -> None:
        tab = tab or self.tab
        entries = tab.selected()
        if not entries:
            return
        folders = [e for e in entries if e.is_dir]
        files = [e for e in entries if not e.is_dir]
        if tab.location == "trash:":
            self._message("Can’t open items in Trash", "Restore them first to open them.", [])
            return
        if len(folders) == 1 and not files and not new_tab:
            self.go(folders[0].path)
        else:
            for f in folders:
                self.new_tab(f.path, switch=False)
        for f in files:
            if servers.is_remote(f.path):
                self.run(servers.OpenJob(f.path))  # downloaded, then opened (see _job_done)
            else:
                self.open_file(f.path)

    def _single_click(self, tab: Tab, index) -> None:
        """Preference "Single click": the clicked item opens (Ctrl / Shift clicks still select)."""
        if self.prefs["click"] != "single" or QApplication.keyboardModifiers() or not index.isValid():
            return
        tab.select_paths([tab.model.rows[index.row()].path])
        self.open_selected(tab)

    def open_file(self, path: str) -> None:
        try:
            system.open_path(path)
        except OSError as e:
            self._message("Unable to open", f"“{os.path.basename(path)}” couldn’t be opened.",
                          [e.strerror or str(e)])

    def open_location(self, e: fs.Entry) -> None:
        """Search results, Recent, Starred: open the folder it's in, with it selected."""
        path = e.trash.original if e.trash else e.path
        self.go(parent_of(path))
        self.tab.select_paths([path])
        self.tab.pending_select = [path]  # a server folder: once its listing arrives

    # ================================================================== the selection label
    def _selection(self, tab: Tab):
        if tab is not self.tab:
            return
        entries = tab.selected()
        if not entries:
            self.status.show_text("")
            return
        if len(entries) == 1:
            e = entries[0]
            size = fs.size_text(e)
            text = f"“{e.name}” selected" + (f" ({size})" if size else "")
        else:
            files = [e for e in entries if not e.is_dir]
            dirs = len(entries) - len(files)
            parts = []
            if dirs:
                parts.append(f"{dirs} folder{'s' if dirs > 1 else ''} selected")
            if files:
                parts.append(f"{len(files)} item{'s' if len(files) > 1 else ''} selected "
                             f"({fs.human_size(sum(e.size for e in files))})")
            text = ", ".join(parts)
        self.status.show_text(text)

    def _thumb_ready(self, path):
        for t in self.tabs:
            t.model.changed(path)

    def _count_ready(self, path, n):
        for t in self.tabs:
            t.model.set_count(path, n)
        if self.tab is not None and self.tab.selected():
            self._selection(self.tab)

    # ================================================================== watching folders
    def _watch(self):
        want = {t.location for t in self.tabs if t.location not in places.SPECIAL and not servers.is_remote(t.location)
                and not places.is_computer(t.location)}
        for t in self.tabs:  # open folders in the list view are watched too
            if t.model.expandable:
                want |= {e.path for e in t.model.rows if e.is_dir and os.path.normcase(e.path) in t.expanded}
        have = set(self.watcher.directories())
        if have - want:
            self.watcher.removePaths(list(have - want))
        add = [p for p in want - have if os.path.isdir(p)]
        if add:
            self.watcher.addPaths(add)

    def _dir_changed(self, path):
        self.dirty.add(os.path.normcase(path))
        self.reload_timer.start()

    def _reload_dirty(self):
        dirty, self.dirty = self.dirty, set()
        for t in self.tabs:
            if t.search is None and t.model.expandable and any(
                    key(e.path) in dirty for e in t.model.rows if e.depth == 0 and e.is_dir):
                dirty.add(key(t.location))  # something changed inside a folder opened in place
            if t.search is None and (key(t.location) in dirty or
                                     t.location in ("trash:", "starred:", "recent:") and "*" in dirty):
                if t.location not in places.SPECIAL and not servers.is_remote(t.location) and \
                        not os.path.isdir(t.location):
                    # The folder went away: go to the nearest one that's still there.
                    p = t.location
                    while p and not os.path.isdir(p):
                        p = os.path.dirname(p)
                    t.location = p or places.home()
                self.load(t)

    def _check_drives(self):
        now = places.drives()
        if [(d.root, d.label) for d in now] != [(d.root, d.label) for d in self.drives]:
            self.drives = now
            self.fill_sidebar()

    def fill_sidebar(self):
        items = places.sidebar()
        # Connected servers, with eject to disconnect, just above Other Locations (where Files lists them).
        for srv in servers.CONNECTED.values():
            items.insert(len(items) - 1, places.Place(srv.label, "server", srv.key + "/", True, "server"))
        self.sidebar.model().set_places(items)
        if self.tab is not None:
            self.sidebar.select_path(self.tab.location)

    # ================================================================== file operations
    def run(self, job: ops.Job, origin: str = "") -> ops.Job:
        """Start a job; `origin` is "undo" / "redo" for jobs that came from those stacks."""
        job.origin = origin
        self.runner.start(job, sync=self.sync_jobs)
        if job in self.runner.running:
            self.tick.start()
            self._tick()
        return job

    def _tick(self):
        jobs = list(self.runner.running)
        hb = self.headerbar
        if not jobs:
            self.tick.stop()
            hb.ops.hide()
            self.popover.hide()
            return
        if not hb.ops.isVisible():
            hb.ops.show()
        hb.ops.fraction = sum(j.fraction() for j in jobs) / len(jobs)
        hb.ops.update()
        self.popover.set_jobs(jobs)
        if self.popover.isVisible():
            self._place_popover()

    def _place_popover(self):
        k = self.headerbar.ops
        pos = k.mapTo(self, k.rect().bottomRight())
        self.popover.move(pos.x() - self.popover.width() + 12, pos.y() + 6)
        self.popover.raise_()

    def _job_done(self, job: ops.Job):
        self._tick()
        for folder in job.changed:
            self.dirty.add(key(folder))
        if job.kind in ("trash", "restore", "delete"):
            self.dirty.add("*")
        self._reload_dirty()
        if job.undo is not None:
            if job.origin == "undo":
                self.redo_stack.append(job.undo)
            else:
                self.undo_stack.append(job.undo)
                if job.origin != "redo":
                    self.redo_stack.clear()
        # Stars follow renames and moves.
        if isinstance(job, ops.RenameJob) and job.state == "done":
            places.follow_star(job.path, job.new_path)
        if isinstance(job, rename.BatchRenameJob):
            for new, old in job.renamed:
                places.follow_star(old, new)
        if isinstance(job, (ops.CopyJob, ops.MovePairsJob)) and job.kind == "move":
            pairs = getattr(job, "moved", None) or [(was, now) for now, was in getattr(job, "pairs", [])]
            for now, was in pairs:
                places.follow_star(was, now)
        # Files selects what was made: a new folder, a renamed item, pasted items.
        select = []
        if isinstance(job, ops.NewFolderJob) and job.state == "done":
            select = [job.path]
        elif isinstance(job, ops.RenameJob) and job.state == "done":
            select = [job.new_path]
        elif isinstance(job, rename.BatchRenameJob) and job.renamed:
            select = [new for new, _old in job.renamed]
        elif isinstance(job, ops.NewDocumentJob) and job.state == "done":
            select = [job.path]
        elif isinstance(job, archive.CompressJob) and job.made:
            select = [job.made]
        elif isinstance(job, archive.ExtractJob) and job.created:
            select = list(job.created)
        elif isinstance(job, ops.CopyJob) and job.origin == "":
            select = job.created or [now for now, _was in job.moved]
        if isinstance(job, servers.OpenJob) and job.state == "done":
            self.open_file(job.local)
        elif isinstance(job, (servers.NewFolderJob,)) and job.state == "done":
            self.tab.pending_select = [job.path]
        elif isinstance(job, servers.RenameJob) and job.state == "done":
            self.tab.pending_select = [job.new_loc]
        if select and self.tab.search is None and key(self.tab.location) == key(parent_of(select[0])):
            self.load(self.tab, keep_selection=False)
            self.tab.select_paths(select)
            self.tab.view.setFocus()
        if isinstance(job, ops.NewDocumentJob) and job.state == "done" and job.origin == "" and select and \
                self.tab.selected() and self.tab.selected()[0].path == job.path:
            QTimer.singleShot(0, self.rename)  # Files opens the rename box on the new file
        if isinstance(job, ops.TrashJob) and job.untrashable:
            names = [os.path.basename(p) for p in job.untrashable]
            if self._ask("confirm", {
                    "title": "Delete permanently",
                    "heading": ("“%s” can’t be put in the Trash. Do you want to delete it "
                                "immediately?" % names[0]) if len(names) == 1 else
                    f"{len(names)} items can’t be put in the Trash. Do you want to delete them immediately?",
                    "text": "This drive has no Trash. If you delete an item, it will be permanently lost.",
                    "button": "Delete"}):
                self.run(ops.DeleteJob(job.untrashable))
        if job.errors:
            self._message("Errors", {"copy": "Some files couldn’t be copied.",
                                     "move": "Some files couldn’t be moved.",
                                     "compress": "The archive couldn’t be made.",
                                     "extract": "The archive couldn’t be extracted."}.get(
                job.kind, "The operation didn’t complete."), job.errors)
        self._chrome()

    def paths_selected(self) -> list[str]:
        return [e.path for e in self.tab.selected() if e.kind not in ("drive", "usb")]

    def copy(self, cut: bool = False) -> None:
        """Ctrl+C / Ctrl+X: the files on the clipboard the way Explorer and GNOME both read them."""
        paths = self.paths_selected() if self.tab.location != "trash:" else []
        if not paths:
            return
        if servers.is_remote(paths[0]):  # server items: their addresses (pasting into a folder downloads them)
            md = QMimeData()
            md.setData(REMOTE_MIME, (("cut" if cut else "copy") + "\n" + "\n".join(paths)).encode("utf-8"))
            md.setText("\n".join(paths))
            QGuiApplication.clipboard().setMimeData(md)
            self._toast(f"{len(paths)} item{'s' if len(paths) > 1 else ''} {'cut' if cut else 'copied'}")
            return
        md = QMimeData()
        md.setUrls([QUrl.fromLocalFile(p) for p in paths])
        md.setData(WINDOWS_EFFECT, struct.pack("<I", 2 if cut else 5))  # DROPEFFECT_MOVE / COPY|LINK (Explorer's)
        md.setData(GNOME_COPIED, ("cut" if cut else "copy").encode() + b"\n" +
                   b"\n".join(QUrl.fromLocalFile(p).toEncoded().data() for p in paths))
        QGuiApplication.clipboard().setMimeData(md)
        self._toast(f"{len(paths)} item{'s' if len(paths) > 1 else ''} {'cut' if cut else 'copied'}" if len(paths) > 1
                    else f"“{os.path.basename(paths[0])}” {'cut' if cut else 'copied'}")

    @staticmethod
    def clipboard_files() -> tuple[list[str], bool]:
        """(local paths or server addresses, cut?) on the clipboard."""
        md = QGuiApplication.clipboard().mimeData()
        if md is not None and md.hasFormat(REMOTE_MIME):
            lines = bytes(md.data(REMOTE_MIME)).decode("utf-8").split("\n")
            return [x for x in lines[1:] if x], lines[0] == "cut"
        if md is None or not md.hasUrls():
            return [], False
        paths = local_paths(md.urls())
        cut = False
        if md.hasFormat(GNOME_COPIED):
            cut = bytes(md.data(GNOME_COPIED)).startswith(b"cut")
        elif md.hasFormat(WINDOWS_EFFECT):
            raw = bytes(md.data(WINDOWS_EFFECT))
            cut = len(raw) >= 4 and struct.unpack("<I", raw[:4])[0] & 2 == 2 and not \
                struct.unpack("<I", raw[:4])[0] & 1
        return paths, cut

    def paste(self, into: str | None = None) -> None:
        paths, cut = self.clipboard_files()
        dest = into or self.tab.location
        if not paths or dest in places.SPECIAL:
            return
        if not self._transfer(paths, dest, cut):
            return
        if cut:
            QGuiApplication.clipboard().clear()  # a cut is pasted once, as in Explorer

    def _transfer(self, paths: list[str], dest: str, move: bool) -> bool:
        """Copy / move between folders and servers: CopyJob between folders, TransferJob to or from a server."""
        src_remote, dest_remote = servers.is_remote(paths[0]), servers.is_remote(dest)
        if src_remote and dest_remote:
            if move and servers.split(paths[0])[0] == servers.split(dest)[0]:
                for p in paths:  # within one server, a move is a rename into the folder
                    job = servers.RenameJob(p, posixpath.basename(servers.split(p)[1].rstrip("/")))
                    job.new_loc = servers.join(servers.split(dest)[0], posixpath.join(
                        servers.split(dest)[1], posixpath.basename(servers.split(p)[1].rstrip("/"))))
                    job.changed.add(dest)
                    self.run(job)
                return True
            self.run(servers.RelayJob(paths, dest, move=move))  # down to a temporary folder, then up
            return True
        if src_remote or dest_remote:
            self.run(servers.TransferJob(paths, dest, move=move))
        else:
            self.run(ops.CopyJob(paths, dest, move=move))
        return True

    def drop(self, paths: list[str], target: str, action: str = "") -> None:
        if servers.is_remote(target) or (paths and servers.is_remote(paths[0])):
            if target in places.SPECIAL:
                return
            self._transfer(paths, target, action == "move")  # between a server and here: copy unless Shift
            return
        if target in places.SPECIAL:
            if target == "trash:":
                self.run(ops.TrashJob(paths))
            return
        if not action:  # Files: same drive moves, another drive copies
            action = "move" if all(fs.same_volume(p, target) for p in paths) else "copy"
        if action == "move" and all(os.path.normcase(os.path.dirname(p)) == os.path.normcase(target) for p in paths):
            return
        self.run(ops.CopyJob(paths, target, move=action == "move"))

    def _sidebar_drop(self, paths, pl, action):
        self.drop(paths, pl.path, action)

    def copy_to(self, move: bool) -> None:
        """Move to… / Copy to…: pick a folder."""
        paths = self.paths_selected()
        if not paths:
            return
        title = "Select Move Destination" if move else "Select Copy Destination"
        start = places.home() if servers.is_remote(paths[0]) else os.path.dirname(paths[0])
        dest = self._ask("choose_folder", {"title": title, "start": start})
        if dest:
            self._transfer(paths, dest, move)

    def trash_selected(self) -> None:
        t = self.tab
        if t.location == "trash:":
            self.delete_from_trash()
            return
        paths = self.paths_selected()
        if paths and servers.is_remote(paths[0]):
            self.delete_selected()  # servers have no Trash: Files asks to delete for good
            return
        if paths and t.location != "other:":
            self.run(ops.TrashJob(paths))

    def delete_selected(self) -> None:
        """Shift+Delete: delete for good, after Files' question."""
        if self.tab.location == "trash:":
            self.delete_from_trash()
            return
        paths = self.paths_selected()
        if not paths or self.tab.location == "other:":
            return
        on_server = servers.is_remote(paths[0])
        name = posixpath.basename(servers.split(paths[0])[1].rstrip("/")) if on_server else os.path.basename(paths[0])
        heading = (f"Are you sure you want to permanently delete “{name}”?"
                   if len(paths) == 1 else
                   f"Are you sure you want to permanently delete the {len(paths)} selected items?")
        text = ("Servers have no Trash. " if on_server else "") + "If you delete an item, it will be permanently lost."
        if self._ask("confirm", {"title": "Delete permanently", "heading": heading, "text": text,
                                 "button": "Delete"}):
            self.run(servers.DeleteJob(paths) if on_server else ops.DeleteJob(paths))

    def delete_from_trash(self) -> None:
        items = [e.trash for e in self.tab.selected() if e.trash]
        if not items:
            return
        heading = (f"Are you sure you want to permanently delete “{items[0].name}” from the Trash?"
                   if len(items) == 1 else
                   f"Are you sure you want to permanently delete the {len(items)} selected items from the Trash?")
        if self._ask("confirm", {"title": "Delete from Trash", "heading": heading,
                                 "text": "If you delete an item, it will be permanently lost.", "button": "Delete"}):
            self.run(ops.TrashDeleteJob(items))

    def restore(self, entries: list[fs.Entry]) -> None:
        items = [e.trash for e in entries if e.trash]
        if items:
            self.run(ops.RestoreJob(items))

    def empty_trash(self) -> None:
        if self._ask("confirm", {"title": "Empty Trash", "heading": "Empty all items from Trash?",
                                 "text": "All items in the Trash will be permanently deleted.",
                                 "button": "Empty Trash"}):
            self.run(ops.TrashDeleteJob(None))

    def rename(self) -> None:
        entries = [e for e in self.tab.selected() if not e.trash and e.kind not in ("drive", "usb")]
        if entries and servers.is_remote(entries[0].path):
            if len(entries) == 1:
                e = entries[0]
                taken = {x.name for x in self.tab.model.rows} - {e.name}
                name = self._ask("name", {"title": "Rename Folder" if e.is_dir else "Rename File",
                                          "label": "Folder name" if e.is_dir else "File name", "button": "Rename",
                                          "folder": "", "initial": e.name, "is_dir": e.is_dir, "current": e.name,
                                          "taken": taken})
                if name and name != e.name and name not in taken:
                    self.run(servers.RenameJob(e.path, name))
            return
        if len(entries) > 1:  # Files' "Rename N Files"
            pairs = self._ask("batch_rename", [e.path for e in entries])
            if pairs:
                self.run(rename.BatchRenameJob(pairs))
            return
        if len(entries) != 1:
            return
        e = entries[0]
        folder = os.path.dirname(e.path)
        name = self._ask("name", {"title": "Rename Folder" if e.is_dir else "Rename File",
                                  "label": "Folder name" if e.is_dir else "File name", "button": "Rename",
                                  "folder": folder, "initial": e.name, "is_dir": e.is_dir, "current": e.name})
        if name and name != e.name and not fs.name_problem(name, folder, e.is_dir, e.name):
            self.run(ops.RenameJob(e.path, name))

    def new_folder(self) -> None:
        folder = self.tab.location
        if folder in places.SPECIAL or self.tab.search is not None:
            return
        if servers.is_remote(folder):
            taken = {x.name for x in self.tab.model.rows}
            name = self._ask("name", {"title": "New Folder", "label": "Folder name", "button": "Create",
                                      "folder": "", "initial": "", "is_dir": True, "current": "", "taken": taken})
            if name and name not in taken:
                self.run(servers.NewFolderJob(folder, name))
            return
        name = self._ask("name", {"title": "New Folder", "label": "Folder name", "button": "Create",
                                  "folder": folder, "initial": "", "is_dir": True, "current": ""})
        if name and not fs.name_problem(name, folder, True):
            self.run(ops.NewFolderJob(folder, name))

    def compress(self) -> None:
        """Files' Compress: name and format asked, the archive made next to the items."""
        paths = self.paths_selected()
        if not paths or self.tab.location in ("trash:", "other:"):
            return
        folder = os.path.dirname(paths[0])
        got = self._ask("compress", {"folder": folder, "name": archive.default_name(paths),
                                     "have_7z": archive.find_7z() is not None})
        if got:
            name, fmt, password = got
            self.run(archive.CompressJob(paths, folder, name, fmt, password))

    def extract(self, path: str, choose: bool = False) -> None:
        """Extract Here (next to the archive) or Extract to… (a folder you pick)."""
        dest = os.path.dirname(path)
        if choose:
            dest = self._ask("choose_folder", {"title": "Select Extract Destination", "start": dest})
            if not dest:
                return
        self.run(archive.ExtractJob(path, dest))

    def windows_menu(self, entries: list[fs.Entry] | None = None, extended: bool = False) -> None:
        """Show More Options: the full Windows right-click menu (other programs' items, Send to...). Opened after
        PR7-E's own menu has closed; what it changes is picked up by the folder watcher."""
        entries = entries if entries is not None else self.tab.selected()
        paths = [e.path for e in entries if e.trash is None and e.kind not in ("drive", "usb")] or \
            [e.path for e in entries if e.kind in ("drive", "usb")]
        if not paths or not winshell.available:
            return

        def show():
            try:
                winshell.show_menu(paths, int(self.winId()), extended)
            except OSError as e:
                self._message("Show More Options", "The Windows menu couldn\u2019t be opened.", [str(e)])
        QTimer.singleShot(0, show)

    def star(self, paths: list[str], on: bool) -> None:
        remote = [p for p in paths if servers.is_remote(p)]
        if remote:  # keep what a server item looks like, for Starred while not connected
            meta = places.star_meta()
            rows = {e.path: e for e in self.tab.model.rows}
            for p in remote:
                if on and p in rows:
                    e = rows[p]
                    meta[p] = {"is_dir": e.is_dir, "size": max(0, e.size), "mtime": e.mtime}
                elif not on:
                    meta.pop(p, None)
            places.set_star_meta(meta)
        places.set_starred(paths, on)
        stars = {os.path.normcase(p) for p in places.starred()}
        for t in self.tabs:
            t.model.stars = stars
            t.view.viewport().update()
            if t.location == "starred:":
                self.load(t)

    def undo(self) -> None:
        if self.undo_stack:
            self.run(self.undo_stack.pop(), origin="undo")

    def redo(self) -> None:
        if self.redo_stack:
            self.run(self.redo_stack.pop(), origin="redo")

    def select_all(self):
        self.tab.view.selectAll()

    def properties(self, entries: list[fs.Entry] | None = None) -> None:
        entries = entries or self.tab.selected()
        if servers.is_remote(self.tab.location):
            if not entries:
                return
            from .dialogs import PropertiesDialog
            dlg = PropertiesDialog(entries, self, server=servers.server_for(entries[0].path))
            if "properties" in self.answers:
                self.answers["properties"](dlg)
                return
            dlg.setAttribute(Qt.WA_DeleteOnClose)
            dlg.show()
            return
        if not entries:
            loc = self.tab.location
            if loc in places.SPECIAL:
                return
            try:
                entries = [fs.entry_for(loc)]
            except OSError:
                return
        from .dialogs import PropertiesDialog
        thumb = None
        if len(entries) == 1 and entries[0].kind == "image":
            img = self.loader.thumbnail(entries[0])
            if img is not None:
                from PySide6.QtGui import QPixmap
                thumb = QPixmap.fromImage(img.scaled(96, 96, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        dlg = PropertiesDialog(entries, self, thumb)
        if "properties" in self.answers:
            self.answers["properties"](dlg)
            return
        dlg.setAttribute(Qt.WA_DeleteOnClose)
        dlg.show()

    def bookmark(self, path: str | None = None) -> None:
        path = path or self.tab.location
        if path in places.SPECIAL:
            return
        if places.add_bookmark(path):
            self.fill_sidebar_all()
            self._toast(f"“{os.path.basename(os.path.normpath(path)) or path}” added to bookmarks")

    def fill_sidebar_all(self):
        for w in WINDOWS:
            w.fill_sidebar()

    def _eject(self, root: str):
        if not places.eject(root):
            self._message("Unable to eject", f"“{root}” couldn’t be ejected.",
                          ["Close any program using it and try again."])
        QTimer.singleShot(1500, self._check_drives)

    # ================================================================== view
    def _expanding(self) -> bool:
        return bool(self.prefs.get("expand_folders")) and self.view_mode == "list"

    def _with_children(self, entries: list[fs.Entry], tab: Tab, depth: int = 0) -> list[fs.Entry]:
        """The rows with each folder opened in place followed by its own (sorted) contents, indented."""
        if not self._expanding() or not tab.expanded:
            return entries
        out = []
        for e in entries:
            e.depth = depth
            out.append(e)
            if e.is_dir and os.path.normcase(e.path) in tab.expanded:
                try:
                    kids = self._sorted(fs.list_dir(e.path, self.show_hidden))
                except OSError:
                    kids = []
                out.extend(self._with_children(kids, tab, depth + 1))
        return out

    def expand_folder(self, tab: Tab, e: fs.Entry, on=None) -> None:
        """Open a folder in place in the list view (or close it, and everything opened inside it). Left on an item
        inside an open folder goes to that folder's row, as in Files."""
        k = os.path.normcase(e.path)
        if on is False and (not e.is_dir or k not in tab.expanded):
            parent = os.path.normcase(os.path.dirname(e.path))
            if parent in tab.expanded:
                tab.select_paths([os.path.dirname(e.path)])
            return
        opening = (k not in tab.expanded) if on is None else on
        if opening:
            if k in tab.expanded:
                return
            tab.expanded.add(k)
        else:
            tab.expanded = {x for x in tab.expanded if x != k and not x.startswith(k + os.sep)}
        keep = [x.path for x in tab.selected()] or [e.path]
        self.load(tab, keep_selection=False)
        tab.select_paths(keep)

    def _sorted(self, entries, key: str | None = None):
        return fs.sort_entries(entries, key or self.sort, self.prefs["folders_first"])

    def _allowed(self, what: str, path: str) -> bool:
        """Files' "Always / On this computer only / Never" preferences (thumbnails, item counts)."""
        v = self.prefs[what]
        if servers.is_remote(path):
            return v == "always"  # each picture would be downloaded: only when asked for "Always"
        return v == "always" or (v == "local" and not places.is_remote(path))

    def _count(self, path: str) -> None:
        if self._allowed("counts", path):
            self.loader.count(path, self.show_hidden)

    def preferences(self) -> None:
        got = self._ask("preferences", dict(self.prefs))
        if got:
            self.set_prefs(got)

    def set_prefs(self, prefs: dict) -> None:
        save_prefs(prefs)
        for w in WINDOWS:
            w.prefs = load_prefs()
            for t in w.tabs:
                w.load(t)

    def set_view(self, mode: str) -> None:
        self.view_mode = mode
        settings.set("view", mode)
        for t in self.tabs:
            keep = [e.path for e in t.selected()]
            t.stack.setCurrentWidget(t.grid if mode == "grid" else t.list)
            if t.model.mode == "folder" and (t.expanded or self.prefs.get("expand_folders")):
                self.load(t)  # the arrows and folders open in place belong to the list view only
            t.select_paths(keep)
        self._chrome()
        self.tab.view.setFocus()

    def set_zoom(self, level: int) -> None:
        self.zoom = max(0, min(len(GridView.ZOOMS) - 1, level))
        settings.set("zoom", self.zoom)
        for t in self.tabs:
            t.grid.set_icon_size(GridView.ZOOMS[self.zoom])

    def toggle_hidden(self, on: bool | None = None) -> None:
        self.show_hidden = (not self.show_hidden) if on is None else on
        settings.set("show_hidden", self.show_hidden)
        self.act["hidden"].setChecked(self.show_hidden)
        for t in self.tabs:
            self.load(t)

    def set_sort(self, key: str) -> None:
        self.sort = key
        settings.set("sort", key)
        for a_key, a in self.sort_actions.items():
            a.setChecked(a_key == key)
        for t in self.tabs:
            if t.model.mode in ("folder", "starred", "trash", "remote"):
                self.load(t)

    def _sort_column(self, key: str) -> None:
        pairs = {"name": ("name", "name_desc"), "size": ("size_desc", "size"), "mtime": ("mtime_desc", "mtime")}
        a, b = pairs[key]
        self.set_sort(b if self.sort == a else a)

    def set_finish(self, name: str) -> None:
        theme.set_finish(name)
        settings.set("finish", theme.FINISH)
        app = QApplication.instance()
        app.setPalette(theme.palette())
        app.setStyleSheet(theme.stylesheet())
        for w in WINDOWS:
            for child in w.findChildren(QWidget):
                if hasattr(child, "restyle"):
                    child.restyle()
                child.update()
            w.update()
        for a_key, a in self.finish_actions.items():
            a.setChecked(a_key == theme.FINISH)

    def set_default_folders(self, on: bool) -> None:
        """Use PR7-E for Folders: double-clicked folders and drives, and Win+E, open PR7-E (per user, undone by
        unticking or by uninstalling)."""
        from . import shell
        if self.shell_reg is None and not shell.available():
            self.act["folders"].setChecked(False)
            return
        if on and not self._ask("confirm", {
                "title": "Use PR7-E for Folders",
                "heading": "Open folders in PR7-E instead of File Explorer?",
                "text": "Double-clicking a folder or drive (on the desktop, in shortcuts and in other programs) and "
                        "Win+E will open PR7-E. The taskbar, Start and Windows' special places (This PC, Control "
                        "Panel, the Recycle Bin's own window) stay with File Explorer. Untick this, or uninstall "
                        "PR7-E, to go back.",
                "button": "Use PR7-E"}):
            self.act["folders"].setChecked(shell.folders_on(self.shell_reg))
            return
        try:
            shell.set_folders(on, self.shell_reg)
        except OSError as e:
            self._message("Use PR7-E for Folders", "The setting couldn\u2019t be changed.", [e.strerror or str(e)])
        self.act["folders"].setChecked(shell.folders_on(self.shell_reg))

    def toggle_sidebar(self):
        self.sidebar.setVisible(not self.sidebar.isVisible())
        settings.set("sidebar", self.sidebar.isVisible())

    # ================================================================== keys and menus
    def _actions(self):
        self.act: dict[str, QAction] = {}

        def a(key, text, slot, shortcuts=(), scope="window", checkable=False):
            act = QAction(text, self)
            if shortcuts:
                act.setShortcuts([QKeySequence(s) for s in shortcuts])
            act.setCheckable(checkable)
            act.triggered.connect(lambda checked=False: slot())
            if scope == "files":
                # File keys belong to the views, so Delete or Ctrl+C in a text field edits the text.
                act.setShortcutContext(Qt.WidgetWithChildrenShortcut)
                self.content.addAction(act)
            else:
                act.setShortcutContext(Qt.WindowShortcut)
                self.addAction(act)
            self.act[key] = act
            return act
        a("new_window", "New Window", lambda: new_window(self.tab.location), ["Ctrl+N"])
        a("new_tab", "New Tab", lambda: self.new_tab(), ["Ctrl+T"])
        a("close_tab", "Close Tab", lambda: self.close_tab(), ["Ctrl+W"])
        a("next_tab", "Next Tab", lambda: self.set_current_tab((self.content.currentIndex() + 1) % len(self.tabs)),
          ["Ctrl+PgDown", "Ctrl+Tab"])
        a("prev_tab", "Previous Tab", lambda: self.set_current_tab((self.content.currentIndex() - 1) % len(self.tabs)),
          ["Ctrl+PgUp", "Ctrl+Shift+Tab"])
        a("quit", "Close Window", self.close, ["Ctrl+Q"])
        a("back", "Back", self.back, ["Alt+Left", "Back"])
        a("back_key", "Back", self.back, ["Backspace"], scope="files")
        a("forward", "Forward", self.forward, ["Alt+Right", "Forward"])
        a("up", "Parent Folder", self.up, ["Alt+Up"])
        a("home", "Home", lambda: self.go(places.home()), ["Alt+Home"])
        a("location", "Enter Location", self.edit_location, ["Ctrl+L"])
        a("search", "Search", lambda: self.end_search() if self.tab.search is not None and
          self.headerbar.search.hasFocus() else self.start_search(), ["Ctrl+F"])
        a("reload", "Reload", self.reload, ["F5", "Ctrl+R"])
        a("hidden", "Show Hidden Files", self.toggle_hidden, ["Ctrl+H"], checkable=True)
        self.act["hidden"].setChecked(self.show_hidden)
        a("grid", "Grid View", lambda: self.set_view("grid"), ["Ctrl+1"])
        a("list", "List View", lambda: self.set_view("list"), ["Ctrl+2"])
        a("zoom_in", "Zoom In", lambda: self.set_zoom(self.zoom + 1), ["Ctrl++", "Ctrl+="])
        a("zoom_out", "Zoom Out", lambda: self.set_zoom(self.zoom - 1), ["Ctrl+-"])
        a("zoom_reset", "Reset Size", lambda: self.set_zoom(2), ["Ctrl+0"])
        a("sidebar", "Show Sidebar", self.toggle_sidebar, ["F9"])
        a("menu", "Main Menu", lambda: self._main_menu(), ["F10"])
        a("bookmark", "Bookmark this Location", self.bookmark, ["Ctrl+D"])
        a("undo", "Undo", self.undo, ["Ctrl+Z"])
        a("redo", "Redo", self.redo, ["Ctrl+Shift+Z", "Ctrl+Y"])
        a("shortcuts", "Keyboard Shortcuts", self._shortcuts, ["Ctrl+?", "Ctrl+F1"])
        a("folders", "Use PR7-E for Folders", lambda: self.set_default_folders(self.act["folders"].isChecked()),
          checkable=True)
        a("open", "Open", self.open_selected, ["Return", "Enter"], scope="files")
        a("open_tab", "Open in New Tab", lambda: self.open_selected(new_tab=True), ["Ctrl+Return"], scope="files")
        a("copy", "Copy", self.copy, ["Ctrl+C"], scope="files")
        a("cut", "Cut", lambda: self.copy(cut=True), ["Ctrl+X"], scope="files")
        a("paste", "Paste", self.paste, ["Ctrl+V"], scope="files")
        a("select_all", "Select All", self.select_all, ["Ctrl+A"], scope="files")
        a("rename", "Rename…", self.rename, ["F2"], scope="files")
        a("trash", "Move to Trash", self.trash_selected, ["Delete"], scope="files")
        a("delete", "Delete Permanently…", self.delete_selected, ["Shift+Delete"], scope="files")
        a("new_folder", "New Folder…", self.new_folder, ["Ctrl+Shift+N"])
        a("properties", "Properties", self.properties, ["Ctrl+I", "Alt+Return"], scope="files")
        a("escape", "Clear", self._escape, ["Escape"], scope="files")
        a("preferences", "Preferences", self.preferences, ["Ctrl+,"])
        a("windows_menu", "Show More Options", lambda: self.windows_menu(), ["Shift+F10"], scope="files")
        self.sort_actions = {}
        for key, text in fs.SORTS.items():
            act = QAction(text, self, checkable=True)
            act.setChecked(key == self.sort)
            act.triggered.connect(lambda _c=False, k=key: self.set_sort(k))
            self.sort_actions[key] = act
        self.finish_actions = {}
        for key in theme.FINISHES:
            act = QAction(key.upper(), self, checkable=True)
            act.setChecked(key == theme.FINISH)
            act.triggered.connect(lambda _c=False, k=key: self.set_finish(k))
            self.finish_actions[key] = act

    def _escape(self):
        if self.tab.search is not None:
            self.end_search()
        else:
            self.tab.view.clearSelection()

    def _key(self, name: str) -> None:
        if name == "back":
            self.back()
        elif name == "forward":
            self.forward()
        elif name == "search":
            if self.tab.search is not None:
                self.end_search()
            else:
                self.start_search()
        elif name == "view":
            self.set_view("list" if self.view_mode == "grid" else "grid")
        elif name == "operations":
            if self.popover.isVisible():
                self.popover.hide()
            else:
                self._tick()
                self._place_popover()
                self.popover.show()
        elif name == "menu":
            self._main_menu()

    def _main_menu(self) -> QMenu:
        """Files' main menu."""
        m = QMenu(self)
        m.addAction(self.act["new_window"])
        m.addAction(self.act["new_tab"])
        m.addSeparator()
        u = m.addAction(f"Undo {self.undo_stack[-1].undo_label}" if self.undo_stack else "Undo", self.undo)
        u.setShortcut(QKeySequence("Ctrl+Z"))
        u.setEnabled(bool(self.undo_stack))
        r = m.addAction(f"Redo {self.redo_stack[-1].undo_label}" if self.redo_stack else "Redo", self.redo)
        r.setShortcut(QKeySequence("Ctrl+Shift+Z"))
        r.setEnabled(bool(self.redo_stack))
        m.addSeparator()
        m.addAction(self.act["hidden"])
        sort = m.addMenu("Sort")
        for act in self.sort_actions.values():
            sort.addAction(act)
        size = m.addMenu("Icon Size")
        for k in ("zoom_in", "zoom_out", "zoom_reset"):
            size.addAction(self.act[k])
        finish = m.addMenu("Finish")
        for act in self.finish_actions.values():
            finish.addAction(act)
        m.addSeparator()
        from . import shell
        self.act["folders"].setChecked(shell.folders_on(self.shell_reg))  # what the registry says now
        self.act["folders"].setEnabled(self.shell_reg is not None or shell.available())
        m.addAction(self.act["folders"])
        m.addSeparator()
        m.addAction(self.act["preferences"])
        m.addAction(self.act["shortcuts"])
        m.addAction("About PR7-E", self._about)
        k = self.headerbar.keys["menu"]
        if "menu" in self.answers:
            self.answers["menu"](m)
        else:
            m.popup(k.mapToGlobal(QPoint(k.width() - m.sizeHint().width(), k.height())))
        return m

    def _view_menu(self, tab: Tab, view, pos) -> QMenu:
        idx = view.indexAt(pos)
        if idx.isValid() and not view.selectionModel().isSelected(idx):
            view.selectionModel().select(idx, QItemSelectionModel.ClearAndSelect | QItemSelectionModel.Rows)
        entries = tab.selected() if idx.isValid() else []
        gpos = view.viewport().mapToGlobal(pos)
        if entries and winshell.available and QApplication.keyboardModifiers() & Qt.ShiftModifier and \
                tab.location not in ("trash:",) and "context" not in self.answers:
            self.windows_menu(entries, extended=True)  # Shift+right-click: Windows' menu with its extended items
            return None
        return self._item_menu(entries, gpos) if entries else self._background_menu(gpos)

    def _item_menu(self, entries: list[fs.Entry], gpos=None) -> QMenu:
        """Files' right-click menu for the selection."""
        t = self.tab
        m = QMenu(self)
        if t.location == "trash:":
            m.addAction("Restore From Trash", lambda: self.restore(entries))
            m.addAction("Delete From Trash…", self.delete_from_trash)
            m.addSeparator()
            m.addAction(self.act["properties"])
            return self._popup(m, gpos)
        if t.location in ("other:", "network:") or places.is_computer(t.location):
            m.addAction("Open", lambda: self.go(entries[0].path))
            m.addAction("Open in New Tab", lambda: self.new_tab(entries[0].path))
            if entries[0].kind == "usb":
                m.addAction("Eject", lambda: self._eject(entries[0].path))
            m.addSeparator()
            m.addAction(self.act["properties"])
            return self._popup(m, gpos)
        files = [e for e in entries if not e.is_dir]
        folders = [e for e in entries if e.is_dir]
        if servers.is_remote(entries[0].path):
            m.addAction("Open", self.open_selected).setShortcut(QKeySequence("Return"))
            if folders and not files:
                m.addAction("Open in New Tab", lambda: self.open_selected(new_tab=True))
            m.addSeparator()
            m.addAction(self.act["cut"])
            m.addAction(self.act["copy"])
            m.addAction("Download to\u2026", lambda: self.copy_to(move=False))
            m.addAction("Delete Permanently\u2026", self.delete_selected)
            if len(folders) == 1 and not files and self.clipboard_files()[0]:
                m.addAction("Paste Into Folder", lambda: self.paste(folders[0].path))
            m.addSeparator()
            r = m.addAction("Rename\u2026", self.rename)
            r.setEnabled(len(entries) == 1)
            m.addSeparator()
            on = all(t.model.starred(e) for e in entries)
            m.addAction("Unstar" if on else "Star", lambda: self.star([e.path for e in entries], not on))
            m.addAction(self.act["properties"])
            return self._popup(m, gpos)
        if folders and not files:
            m.addAction("Open", self.open_selected).setShortcut(QKeySequence("Return"))
            m.addAction("Open in New Tab", lambda: self.open_selected(new_tab=True)).setShortcut(
                QKeySequence("Ctrl+Return"))
            m.addAction("Open in New Window", lambda: [new_window(e.path) for e in folders])
        else:
            app = system.default_app(files[0].path) if len(files) == 1 else None
            m.addAction(f"Open With {app}" if app else "Open", self.open_selected).setShortcut(QKeySequence("Return"))
            if len(files) == 1:
                m.addAction("Open With Other Application", lambda: system.open_with(files[0].path))
        if t.search is not None or t.location in ("recent:", "starred:"):
            if len(entries) == 1:
                m.addAction("Open Item Location", lambda: self.open_location(entries[0]))
        m.addSeparator()
        m.addAction(self.act["cut"])
        m.addAction(self.act["copy"])
        m.addAction("Move to…", lambda: self.copy_to(move=True))
        m.addAction("Copy to…", lambda: self.copy_to(move=False))
        m.addAction(self.act["trash"])
        if self.prefs["show_delete"]:
            m.addAction(self.act["delete"])
        if len(folders) == 1 and not files and self.clipboard_files()[0]:
            m.addAction("Paste Into Folder", lambda: self.paste(folders[0].path))
        m.addSeparator()
        m.addAction(self.act["rename"])  # one item: Rename; several: Rename N Files
        m.addAction("Compress…", self.compress)
        if len(files) == 1 and not folders and archive.can_extract(files[0].name):
            m.addAction("Extract Here", lambda: self.extract(files[0].path))
            m.addAction("Extract to…", lambda: self.extract(files[0].path, choose=True))
        m.addSeparator()
        if len(files) == 1 and not folders and files[0].name.lower().endswith(places.WALLPAPER_EXTS):
            m.addAction("Set as Background\u2026", lambda: self.set_background(files[0].path))
        on = all(t.model.starred(e) for e in entries)
        m.addAction("Unstar" if on else "Star", lambda: self.star([e.path for e in entries], not on))
        m.addAction(self.act["properties"])
        m.addSeparator()
        more = m.addAction("Show More Options", lambda: self.windows_menu(entries))
        more.setShortcut(QKeySequence("Shift+F10"))
        more.setEnabled(winshell.available and winshell.same_folder([e.path for e in entries]))
        return self._popup(m, gpos)

    def _templates_menu(self, menu: QMenu, items) -> None:
        """Files' New Document submenu: the Templates folder (folders as submenus), an empty document, and a way
        to the folder to add your own."""
        for name, path, kids in items:
            if kids:
                self._templates_menu(menu.addMenu(name), kids)
            else:
                menu.addAction(name, lambda p=path: self.new_document(p))
        if menu.title() == "New Document":
            if items:
                menu.addSeparator()
            menu.addAction("Empty Document", lambda: self.new_document(None))
            menu.addSeparator()
            menu.addAction("Open Templates Folder", lambda: self.go(places.templates_dir()))

    def new_document(self, template: str | None) -> None:
        """A new file from a template (or empty) in the folder shown; then Rename, as Files does."""
        folder = self.tab.location
        if folder in places.SPECIAL or servers.is_remote(folder) or self.tab.search is not None:
            return
        self.run(ops.NewDocumentJob(folder, template))

    def set_background(self, path: str) -> None:
        if not places.set_wallpaper(path):
            self._message("Set as Background", "The picture couldn\u2019t be set as the background.", [])

    def _background_menu(self, gpos=None) -> QMenu:
        """Files' menu for the folder itself (empty space, and the path bar's folder menu)."""
        t = self.tab
        m = QMenu(self)
        if t.location == "trash:":
            m.addAction("Empty Trash…", self.empty_trash).setEnabled(bool(t.model.rows))
            m.addAction(self.act["select_all"])
            return self._popup(m, gpos)
        real = t.location not in places.SPECIAL and t.search is None
        # Menu-only copies: disabling the window's own actions would switch their keys off too.
        nf = m.addAction("New Folder\u2026", self.new_folder)
        nf.setShortcut(QKeySequence("Ctrl+Shift+N"))
        nf.setEnabled(real)
        if real and not servers.is_remote(t.location):
            self._templates_menu(m.addMenu("New Document"), places.templates())  # Files: under New Folder
        p = m.addAction("Paste", self.paste)
        p.setShortcut(QKeySequence("Ctrl+V"))
        p.setEnabled(real and bool(self.clipboard_files()[0]))
        m.addAction(self.act["select_all"])
        m.addSeparator()
        if real and servers.is_remote(t.location):
            m.addAction(f"Disconnect from {servers.server_for(t.location).label}" if servers.server_for(t.location)
                        else "Disconnect", lambda: self.disconnect_server(servers.split(t.location)[0]))
        elif real:
            m.addAction("Add to Bookmarks", self.bookmark).setShortcut(QKeySequence("Ctrl+D"))
            m.addAction("Open in Terminal", lambda: system.terminal(t.location))
            m.addSeparator()
            m.addAction(self.act["properties"])
        return self._popup(m, gpos)

    def _sidebar_menu(self, pos) -> QMenu | None:
        pl = self.sidebar.place_at(pos)
        if pl is None:
            return None
        m = QMenu(self)
        m.addAction("Open", lambda: self.go(pl.path))
        m.addAction("Open in New Tab", lambda: self.new_tab(pl.path))
        m.addAction("Open in New Window", lambda: new_window(pl.path))
        if pl.kind == "bookmark":
            m.addSeparator()
            m.addAction("Rename…", lambda: self._rename_bookmark(pl))
            m.addAction("Remove from Bookmarks", lambda: (places.remove_bookmark(pl.path), self.fill_sidebar_all()))
        if pl.kind == "server":
            m.addSeparator()
            m.addAction("Disconnect", lambda: self.disconnect_server(pl.path))
        elif pl.kind == "share":
            m.addSeparator()
            m.addAction("Disconnect", lambda: self.forget_share(pl.path))
        elif pl.eject:
            m.addSeparator()
            m.addAction("Eject", lambda: self._eject(pl.path))
        if pl.path == "trash:":
            m.addSeparator()
            m.addAction("Empty Trash…", self.empty_trash)
        return self._popup(m, self.sidebar.viewport().mapToGlobal(pos))

    def _rename_bookmark(self, pl):
        name = self._ask("name", {"title": "Rename Bookmark", "label": "Name", "button": "Rename", "folder": "",
                                  "initial": pl.label, "is_dir": True, "current": pl.label, "bookmark": True})
        if name:
            items = places.bookmarks()
            for b in items:
                if os.path.normcase(b["path"]) == os.path.normcase(pl.path):
                    b["name"] = name
            places.set_bookmarks(items)
            self.fill_sidebar_all()

    def _popup(self, m: QMenu, gpos) -> QMenu:
        if "context" in self.answers:
            self.answers["context"](m)
        elif gpos is not None:
            m.popup(gpos)
        return m

    # ================================================================== questions and messages
    def _ask(self, kind: str, payload):
        if kind in self.answers:
            return self.answers[kind](payload)
        from . import dialogs
        if kind == "conflict":
            dlg = dialogs.ConflictDialog(payload, self)
            return dlg.answer() if dlg.exec() else None
        if kind == "name":
            if payload.get("bookmark"):
                from PySide6.QtWidgets import QInputDialog
                text, ok = QInputDialog.getText(self, payload["title"], payload["label"], text=payload["initial"])
                return text.strip() if ok and text.strip() else None
            dlg = dialogs.NameDialog(payload["title"], payload["label"], payload["button"], payload["folder"],
                                     payload["initial"], payload["is_dir"], payload["current"], self,
                                     payload.get("taken"))
            return dlg.answer() if dlg.exec() else None
        if kind == "confirm":
            dlg = dialogs.ConfirmDialog(payload["title"], payload["heading"], payload["text"], payload["button"],
                                        self)
            return bool(dlg.exec())
        if kind == "login":
            dlg = dialogs.LoginDialog(payload["host"], payload["user"], payload["protocol"], self)
            return dlg.answer() if dlg.exec() else None
        if kind == "hostkey":
            host, port, key_type, fingerprint, changed = payload
            return "always" if self._ask("confirm", {
                "title": "Identity changed" if changed else "Can\u2019t verify identity",
                "heading": (f"The identity of \u201c{host}\u201d has CHANGED." if changed else
                            f"Can\u2019t verify the identity of \u201c{host}\u201d."),
                "text": ("Someone could be pretending to be the server. If you didn\u2019t expect this, cancel and ask "
                         "the server\u2019s administrator. " if changed else "This happens when you log in to a "
                         "computer the first time. ") + f"The identity sent by the server is {key_type} {fingerprint}.",
                "button": "Log In Anyway"}) else None
        if kind == "cert":
            host, info = payload
            return bool(self._ask("confirm", {
                "title": "Can\u2019t verify identity", "heading": f"Can\u2019t verify the identity of \u201c{host}\u201d.",
                "text": "The server\u2019s certificate isn\u2019t trusted by Windows (common for small servers). "
                        f"{info if isinstance(info, str) else ''}", "button": "Log In Anyway"}))
        if kind == "preferences":
            dlg = dialogs.PreferencesDialog(payload, self)
            return dlg.answer() if dlg.exec() else None
        if kind == "batch_rename":
            dlg = dialogs.BatchRenameDialog(payload, self)
            return dlg.answer() if dlg.exec() else None
        if kind == "compress":
            dlg = dialogs.CompressDialog(payload["folder"], payload["name"], payload["have_7z"], self)
            return dlg.answer() if dlg.exec() else None
        if kind == "password":
            dlg = dialogs.PasswordDialog(payload, self)
            return dlg.answer() if dlg.exec() else None
        if kind == "choose_folder":
            return QFileDialog.getExistingDirectory(self, payload["title"], payload["start"]) or None
        if kind == "message":
            dialogs.MessageDialog(*payload, self).exec()
            return None
        return None

    def _message(self, title: str, heading: str, lines: list[str]) -> None:
        self._ask("message", (title, heading, lines))

    def _toast(self, text: str) -> None:
        """A short note in the corner (Files' toasts), until the selection changes."""
        self.status.show_text(text)

    def _shortcuts(self):
        from .dialogs import ShortcutsDialog
        ShortcutsDialog(self).exec()

    def _about(self):
        from .dialogs import AboutDialog
        AboutDialog(self).exec()

    # ================================================================== window
    def resizeEvent(self, e):
        super().resizeEvent(e)
        self.status.place()
        self.empty.setGeometry(self.content.rect())
        if self.popover.isVisible():
            self._place_popover()

    def showEvent(self, e):
        super().showEvent(e)
        self.empty.setGeometry(self.content.rect())
        self.status.place()

    def closeEvent(self, e):
        QApplication.instance().removeEventFilter(self)
        if self.runner.running:
            if not self._ask("confirm", {"title": "Operations running", "heading": "Close PR7-E while files are "
                                         "still being copied or moved?", "text": "Running operations will be "
                                         "cancelled.", "button": "Cancel Operations"}):
                e.ignore()
                return
            for j in self.runner.running:
                j.cancel()
        settings.set("geometry", bytes(self.saveGeometry()).hex())
        self.searcher.stop()
        self.loader.clear_queue()
        if self in WINDOWS:
            WINDOWS.remove(self)
        super().closeEvent(e)


PREFS = {"folders_first": True, "click": "double", "show_delete": False, "search_subfolders": True,
         "expand_folders": False,
         "thumbnails": "local", "counts": "local"}


def load_prefs() -> dict:
    """Files' preferences, with its defaults."""
    out = {}
    for k, default in PREFS.items():
        if isinstance(default, bool):
            out[k] = settings.get_bool("pref_" + k, default)
        else:
            v = settings.get("pref_" + k, default)
            out[k] = v if v in {"click": ("double", "single")}.get(k, ("always", "local", "never")) else default
    return out


def save_prefs(prefs: dict) -> None:
    for k in PREFS:
        if k in prefs:
            settings.set("pref_" + k, prefs[k])


def new_window(location: str | None = None) -> MainWindow:
    w = MainWindow([location] if location else None)
    w.show()
    return w
