"""PR7-E's dialogs, in GNOME Files' words: rename / new folder, the "Replace file?" question, confirmations,
Properties, keyboard shortcuts and About. Each answers through answer() so the window's `answers` can stand in
for it in tests."""

from __future__ import annotations

import os
import threading

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (QCheckBox, QDialog, QDialogButtonBox, QGridLayout, QHBoxLayout, QLabel, QLineEdit,
                               QPushButton, QVBoxLayout, QWidget)

from . import __version__, fs, places, system, theme
from .widgets import Header, file_pixmap


def _shell(dlg: QDialog, header: str) -> QVBoxLayout:
    root = QVBoxLayout(dlg)
    root.setContentsMargins(0, 0, 0, 16)
    root.addWidget(Header(header))
    body = QVBoxLayout()
    body.setContentsMargins(22, 14, 22, 0)
    body.setSpacing(10)
    root.addLayout(body, 1)
    return body


def _field(text: str) -> QLabel:
    lab = QLabel(text.upper())
    lab.setProperty("role", "field")
    return lab


class NameDialog(QDialog):
    """Files' Rename and New Folder popover as a small dialog: the name (Rename selects it without the
    extension), Files' reason when it can't be used, and Rename / Create."""

    def __init__(self, title: str, label: str, button: str, folder: str, initial: str = "", is_dir: bool = False,
                 current: str = "", parent=None, taken: set | None = None):
        """`taken`: the names already there, for a server folder (folder "" then: nothing is looked up on disk)."""
        super().__init__(parent)
        self.setWindowTitle(title)
        self.folder, self.is_dir, self.current = folder, is_dir, current
        self.taken = taken
        self.resize(520, 0)
        body = _shell(self, title.upper())
        body.addWidget(_field(label))
        self.edit = QLineEdit(initial)
        body.addWidget(self.edit)
        self.problem = QLabel("")
        self.problem.setProperty("role", "error")
        self.problem.setWordWrap(True)
        body.addWidget(self.problem)
        self.buttons = QDialogButtonBox(QDialogButtonBox.Cancel)
        self.ok = self.buttons.addButton(button, QDialogButtonBox.AcceptRole)
        self.ok.setDefault(True)
        self.buttons.accepted.connect(self.accept)
        self.buttons.rejected.connect(self.reject)
        body.addWidget(self.buttons)
        self.edit.textChanged.connect(self._check)
        stem, _ext = fs.split_ext(initial, is_dir)
        self.edit.setSelection(0, len(stem))
        self._check()

    def _check(self):
        name = self.edit.text()
        if self.taken is not None:
            problem = fs.name_problem(name, os.path.join(os.sep, "\0nowhere"), self.is_dir, self.current)
            if not problem and name in self.taken:
                problem = f"A {'folder' if self.is_dir else 'file'} with that name already exists."
        else:
            problem = fs.name_problem(name, self.folder, self.is_dir, self.current)
        self.problem.setText(problem)
        self.ok.setEnabled(bool(name.strip()) and not problem and name != self.current)

    def accept(self):
        if self.ok.isEnabled():
            super().accept()

    def answer(self) -> str:
        return self.edit.text()


class ConflictDialog(QDialog):
    """Files' "Replace file?" / "Merge folder?": both items' size and date, Cancel / Skip / Replace (Merge),
    a new name instead, and "Apply this action to all files and folders"."""

    def __init__(self, c, parent=None):
        super().__init__(parent)
        self.c = c
        name = os.path.basename(c.target)
        folder = os.path.basename(os.path.dirname(c.target)) or os.path.dirname(c.target)
        kind = "folder" if c.target_dir else "file"
        self.setWindowTitle("Merge folder" if c.merge else f"Replace {kind}")
        self.resize(600, 0)
        body = _shell(self, "MERGE FOLDER" if c.merge else f"REPLACE {kind.upper()}")
        head = QLabel(f"{'Merge' if c.merge else 'Replace'} {kind} “{name}”?")
        head.setProperty("role", "heading")
        body.addWidget(head)
        text = (f"Another folder with the same name already exists in “{folder}”. Merging will ask for "
                "confirmation before replacing any files in the folder that conflict with the files being copied."
                if c.merge else f"Another {kind} with the same name already exists in “{folder}”. "
                "Replacing it will overwrite its content.")
        t = QLabel(text)
        t.setWordWrap(True)
        t.setProperty("role", "dim")
        body.addWidget(t)
        g = QGridLayout()
        g.setHorizontalSpacing(14)
        for r, (label, is_dir, size, mtime, path) in enumerate((
                ("Original " + kind, c.target_dir, c.target_size, c.target_mtime, c.target),
                ("Replace with", c.source_dir, c.source_size, c.source_mtime, c.source))):
            icon = QLabel()
            icon.setPixmap(file_pixmap(fs.kind_of(path, is_dir), 40))
            g.addWidget(icon, r, 0)
            what = "" if is_dir else fs.human_size(size) + "  ·  "
            info = QLabel(f"<b>{label}</b><br><span style='color:{theme.TEXT_DIM}'>{what}Last modified: "
                          f"{fs.full_date(mtime)}</span>")
            g.addWidget(info, r, 1)
        g.setColumnStretch(1, 1)
        body.addLayout(g)
        body.addWidget(_field("Select a new name for the destination"))
        row = QHBoxLayout()
        self.name = QLineEdit(name)
        reset = QPushButton("Reset")
        reset.clicked.connect(lambda: self.name.setText(name))
        row.addWidget(self.name, 1)
        row.addWidget(reset)
        body.addLayout(row)
        self.problem = QLabel("")
        self.problem.setProperty("role", "error")
        body.addWidget(self.problem)
        self.all = QCheckBox("Apply this action to all files and folders")
        body.addWidget(self.all)
        bb = QHBoxLayout()  # Files' order, left to right, on every system (a button box reorders them)
        bb.addStretch(1)
        cancel, skip = QPushButton("Cancel"), QPushButton("Skip")
        self.go = QPushButton("Merge" if c.merge else "Replace")
        for b in (cancel, skip, self.go):
            bb.addWidget(b)
        self.go.setDefault(True)
        cancel.clicked.connect(self.reject)
        skip.clicked.connect(lambda: self._done("skip"))
        self.go.clicked.connect(lambda: self._done(self._action()))
        body.addLayout(bb)
        self.result_action = None
        self.name.textChanged.connect(self._renamed)

    def _action(self):
        return "rename" if self.name.text() != os.path.basename(self.c.target) else (
            "merge" if self.c.merge else "replace")

    def _renamed(self):
        renaming = self._action() == "rename"
        problem = fs.name_problem(self.name.text(), os.path.dirname(self.c.target), self.c.source_dir) if renaming \
            else ""
        self.problem.setText(problem)
        self.go.setText("Rename" if renaming else ("Merge" if self.c.merge else "Replace"))
        self.go.setEnabled(bool(self.name.text().strip()) and not problem)
        self.all.setEnabled(not renaming)

    def _done(self, action):
        self.result_action = action
        self.accept()

    def answer(self):
        if self.result_action == "rename":
            return ("rename", self.name.text())
        return (self.result_action, self.all.isChecked())


class ConfirmDialog(QDialog):
    """A question with one action ("Delete", "Empty Trash") and Cancel."""

    def __init__(self, title: str, heading: str, text: str, button: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(title)
        self.resize(520, 0)
        body = _shell(self, title.upper())
        h = QLabel(heading)
        h.setProperty("role", "heading")
        h.setWordWrap(True)
        body.addWidget(h)
        t = QLabel(text)
        t.setWordWrap(True)
        t.setProperty("role", "dim")
        body.addWidget(t)
        bb = QDialogButtonBox(QDialogButtonBox.Cancel)
        ok = bb.addButton(button, QDialogButtonBox.AcceptRole)
        ok.setDefault(True)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        body.addWidget(bb)


class MessageDialog(QDialog):
    def __init__(self, title: str, heading: str, lines: list[str], parent=None):
        super().__init__(parent)
        self.setWindowTitle(title)
        self.resize(560, 0)
        body = _shell(self, title.upper())
        h = QLabel(heading)
        h.setProperty("role", "heading")
        body.addWidget(h)
        shown = lines[:12] + ([f"…and {len(lines) - 12} more"] if len(lines) > 12 else [])
        t = QLabel("\n".join(shown))
        t.setWordWrap(True)
        t.setProperty("role", "dim")
        body.addWidget(t)
        bb = QDialogButtonBox(QDialogButtonBox.Ok)
        bb.accepted.connect(self.accept)
        body.addWidget(bb)


class PropertiesDialog(QDialog):
    """Files' Properties: the icon and name, size (folders are counted in the background: "12 items, totalling
    4.2 GB"), type, parent folder, dates, and the drive's free space."""

    def __init__(self, entries: list[fs.Entry], parent=None, thumb: QPixmap | None = None, server=None):
        """`server`: a servers.Server when the items are in one of its folders (counted there, no disk lookups)."""
        super().__init__(parent)
        self.entries = entries
        self.server = server
        one = entries[0] if len(entries) == 1 else None
        self.setWindowTitle(f"{one.name} Properties" if one else "Properties")
        self.resize(560, 0)
        body = _shell(self, "PROPERTIES")
        top = QHBoxLayout()
        icon = QLabel()
        icon.setPixmap(thumb if thumb is not None else file_pixmap(one.kind if one else "file", 64))
        top.addWidget(icon)
        name = QLabel(one.name if one else f"{len(entries)} items")
        name.setProperty("role", "heading")
        name.setWordWrap(True)
        name.setTextInteractionFlags(Qt.TextSelectableByMouse)
        top.addWidget(name, 1)
        body.addLayout(top)
        self.grid = QGridLayout()
        self.grid.setHorizontalSpacing(18)
        self.grid.setVerticalSpacing(8)
        self.rows = 0
        self.size = self._row("Size", "")
        folder = os.path.dirname(entries[0].path) if server is None else entries[0].path.rsplit("/", 1)[0]
        if one:
            self._row("Type", system.type_name(one.path, one.is_dir))
        else:
            self._row("Contents", fs.items_text(len(entries)))
        self._row("Parent folder", folder)
        if server is not None:
            if one and one.mtime:
                self._row("Modified", fs.full_date(one.mtime))
            if one and one.extra:
                self._row("Permissions", one.extra)
            self._row("Server", server.key)
        elif one:
            try:
                st = os.stat(one.path)
                self._row("Modified", fs.full_date(st.st_mtime))
                self._row("Accessed", fs.full_date(st.st_atime))
                created = getattr(st, "st_birthtime", None) or (st.st_ctime if os.name == "nt" else None)
                if created:
                    self._row("Created", fs.full_date(created))
            except OSError:
                pass
        d = places.drive_of(entries[0].path) if server is None else None
        if d and d.total:
            self._row("Free space", f"{fs.human_size(d.free)} free of {fs.human_size(d.total)}")
        body.addLayout(self.grid)
        bb = QDialogButtonBox(QDialogButtonBox.Close)
        bb.rejected.connect(self.reject)
        bb.accepted.connect(self.accept)
        body.addWidget(bb)
        self.counted = None
        self._stop = threading.Event()
        if any(e.is_dir for e in entries):
            self.size.setText("Counting…")
            self._partial = (0, 0, 0)
            threading.Thread(target=self._count, daemon=True).start()
            self.timer = QTimer(self)
            self.timer.timeout.connect(self._show_count)
            self.timer.start(150)
        else:
            total = sum(e.size for e in entries)
            self.size.setText(f"{fs.human_size(total)} ({total:,} bytes)")

    def _row(self, label, value) -> QLabel:
        self.grid.addWidget(_field(label), self.rows, 0, Qt.AlignTop)
        v = QLabel(value)
        v.setWordWrap(True)
        v.setTextInteractionFlags(Qt.TextSelectableByMouse)
        self.grid.addWidget(v, self.rows, 1)
        self.grid.setColumnStretch(1, 1)
        self.rows += 1
        return v

    def _count(self):
        def progress(b, f, d):
            self._partial = (b, f, d)
        dirs = [e.path for e in self.entries if e.is_dir]
        if self.server is not None:
            from . import servers
            try:
                b, f, d = servers.tree_size(self.server, [servers.split(p)[1] for p in dirs], self._stop.is_set,
                                            progress)
            except Exception:  # noqa: BLE001 - the server went away: what was counted
                b, f, d = self._partial
        else:
            b, f, d = fs.tree_size(dirs, self._stop.is_set, progress)
        b += sum(e.size for e in self.entries if not e.is_dir)
        f += sum(1 for e in self.entries if not e.is_dir)
        self.counted = (b, f, d)

    def _show_count(self):
        b, f, d = self.counted or self._partial
        dirs = sum(1 for e in self.entries if e.is_dir)
        items = f + max(0, d - dirs)  # what's inside, not the folders themselves
        if len(self.entries) > 1:
            items = f + d
        self.size.setText(f"{fs.items_text(items)}, totalling {fs.human_size(b)}" + ("" if self.counted else "…"))
        if self.counted:
            self.timer.stop()

    def done(self, r):
        self._stop.set()
        super().done(r)


SHORTCUTS = [
    ("Opening", [("New window", "Ctrl+N"), ("New tab", "Ctrl+T"), ("Close tab", "Ctrl+W"),
                 ("Go to previous / next tab", "Ctrl+Page Up / Ctrl+Page Down"), ("Open in new tab", "Ctrl+Return"),
                 ("Close window", "Ctrl+Q")]),
    ("Navigation", [("Back / forward", "Alt+Left / Alt+Right"), ("Back", "Backspace"), ("Parent folder", "Alt+Up"),
                    ("Home", "Alt+Home"), ("Enter a location", "Ctrl+L"), ("Search", "Ctrl+F or start typing"),
                    ("Reload", "F5 / Ctrl+R"), ("Bookmark this location", "Ctrl+D")]),
    ("View", [("Grid view / list view", "Ctrl+1 / Ctrl+2"), ("Zoom in / out / reset", "Ctrl++ / Ctrl+- / Ctrl+0"),
              ("Show hidden files", "Ctrl+H"), ("Show sidebar", "F9"), ("Main menu", "F10")]),
    ("Editing", [("Copy / cut / paste", "Ctrl+C / Ctrl+X / Ctrl+V"), ("Select all", "Ctrl+A"),
                 ("New folder", "Ctrl+Shift+N"), ("Rename", "F2"), ("Move to Trash", "Delete"),
                 ("Delete permanently", "Shift+Delete"), ("Undo / redo", "Ctrl+Z / Ctrl+Shift+Z"),
                 ("Properties", "Ctrl+I / Alt+Return")]),
]


class ShortcutsDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Keyboard Shortcuts")
        self.resize(900, 0)
        body = _shell(self, "KEYBOARD SHORTCUTS")
        cols = QHBoxLayout()
        cols.setSpacing(30)
        for i in (0, 2):
            col = QVBoxLayout()
            for group, items in SHORTCUTS[i:i + 2]:
                col.addWidget(_field(group))
                g = QGridLayout()
                for r, (what, keys) in enumerate(items):
                    g.addWidget(QLabel(what), r, 0)
                    k = QLabel(keys)
                    k.setStyleSheet(f"color: {theme.TEXT_DIM};")
                    g.addWidget(k, r, 1, Qt.AlignRight)
                col.addLayout(g)
                col.addSpacing(10)
            col.addStretch(1)
            cols.addLayout(col, 1)
        body.addLayout(cols)
        bb = QDialogButtonBox(QDialogButtonBox.Close)
        bb.rejected.connect(self.reject)
        body.addWidget(bb)


class AboutDialog(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle("About PR7-E")
        self.resize(520, 0)
        body = _shell(self, f"VERSION {__version__}")
        t = QLabel("A file manager that works the way GNOME Files (Ubuntu's file manager) works, in the Paragon "
                   "family look.\n\nPySide6 (Qt for Python), LGPL. Inter typeface, SIL Open Font License.")
        t.setWordWrap(True)
        body.addWidget(t)
        bb = QDialogButtonBox(QDialogButtonBox.Close)
        bb.rejected.connect(self.reject)
        body.addWidget(bb)


class CompressDialog(QDialog):
    """Files' "Create Compressed Archive": the name, the format (with Files' notes on each), a password for the
    encrypted ones. Formats that need 7-Zip are off when it isn't there."""

    NOTES = {".zip": "Compatible with all operating systems.",
             "Encrypted .zip": "Password protected (AES-256). Opens in 7-Zip, PR7-Z and most archive programs.",
             ".tar.xz": "Smaller archives, but Linux and Mac only.",
             ".7z": "Smaller archives, but need 7-Zip, PR7-Z or similar to open."}

    def __init__(self, folder: str, name: str, have_7z: bool, parent=None):
        super().__init__(parent)
        from PySide6.QtWidgets import QComboBox
        from . import archive
        self.folder = folder
        self.setWindowTitle("Create Compressed Archive")
        self.resize(560, 0)
        body = _shell(self, "COMPRESS")
        body.addWidget(_field("Archive name"))
        row = QHBoxLayout()
        self.name = QLineEdit(name)
        self.fmt = QComboBox()
        for label, _ext, needs, _enc in archive.FORMATS:
            self.fmt.addItem(label)
            if needs and not have_7z:
                self.fmt.model().item(self.fmt.count() - 1).setEnabled(False)
        row.addWidget(self.name, 1)
        row.addWidget(self.fmt)
        body.addLayout(row)
        self.note = QLabel("")
        self.note.setProperty("role", "dim")
        self.note.setWordWrap(True)
        body.addWidget(self.note)
        self.pw_label = _field("Password")
        self.password = QLineEdit()
        self.password.setEchoMode(QLineEdit.Password)
        body.addWidget(self.pw_label)
        body.addWidget(self.password)
        if not have_7z:
            n = QLabel("Encrypted .zip and .7z need 7-Zip’s engine: install PR7-Z to use them.")
            n.setProperty("role", "dim")
            n.setWordWrap(True)
            body.addWidget(n)
        self.problem = QLabel("")
        self.problem.setProperty("role", "error")
        body.addWidget(self.problem)
        bb = QDialogButtonBox(QDialogButtonBox.Cancel)
        self.ok = bb.addButton("Create", QDialogButtonBox.AcceptRole)
        self.ok.setDefault(True)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        body.addWidget(bb)
        for sig in (self.name.textChanged, self.fmt.currentIndexChanged, self.password.textChanged):
            sig.connect(self._check)
        self.name.selectAll()
        self._check()

    def _ext(self) -> str:
        from . import archive
        return next(f[1] for f in archive.FORMATS if f[0] == self.fmt.currentText())

    def _check(self, *_):
        enc = self.fmt.currentText() == "Encrypted .zip"
        self.pw_label.setVisible(enc)
        self.password.setVisible(enc)
        self.note.setText(self.NOTES[self.fmt.currentText()])
        name = self.name.text().strip()
        problem = fs.name_problem(name + self._ext(), self.folder) if name else ""
        self.problem.setText(problem)
        self.ok.setEnabled(bool(name) and not problem and (not enc or bool(self.password.text())))

    def accept(self):
        if self.ok.isEnabled():
            super().accept()

    def answer(self):
        return (self.name.text().strip(), self.fmt.currentText(), self.password.text())


class PasswordDialog(QDialog):
    """An archive needs its password."""

    def __init__(self, name: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Password Required")
        self.resize(480, 0)
        body = _shell(self, "PASSWORD REQUIRED")
        t = QLabel(f"“{name}” is password protected.")
        t.setWordWrap(True)
        body.addWidget(t)
        self.edit = QLineEdit()
        self.edit.setEchoMode(QLineEdit.Password)
        body.addWidget(self.edit)
        bb = QDialogButtonBox(QDialogButtonBox.Cancel)
        ok = bb.addButton("Extract", QDialogButtonBox.AcceptRole)
        ok.setDefault(True)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        body.addWidget(bb)

    def answer(self):
        return self.edit.text()


class BatchRenameDialog(QDialog):
    """Files' "Rename N Files": a template (with tags added from the menu) or find and replace, the numbering
    order, and the preview: each name and what it becomes, problems in red. Rename is off until every new name can
    be used."""

    def __init__(self, paths: list[str], parent=None):
        super().__init__(parent)
        from PySide6.QtGui import QColor
        from PySide6.QtWidgets import QComboBox, QMenu, QRadioButton, QTreeWidget
        from . import rename
        self.rename = rename
        self.paths = paths
        self.QColor = QColor
        self.setWindowTitle(f"Rename {len(paths)} Files")
        self.resize(820, 620)
        body = _shell(self, f"RENAME {len(paths)} FILES")
        modes = QHBoxLayout()
        self.use_template = QRadioButton("Rename using a template")
        self.use_replace = QRadioButton("Find and replace text")
        self.use_template.setChecked(True)
        modes.addWidget(self.use_template)
        modes.addWidget(self.use_replace)
        modes.addStretch(1)
        body.addLayout(modes)
        self.template_row = QWidget()
        tr = QHBoxLayout(self.template_row)
        tr.setContentsMargins(0, 0, 0, 0)
        self.template = QLineEdit(f"{rename.TAG_NAME}")
        add = QPushButton("+ Add")
        m = QMenu(add)
        for tag in rename.TAGS:
            m.addAction(tag, lambda t=tag: self.template.insert(t))
        add.setMenu(m)
        self.order = QComboBox()
        for key, text in rename.ORDERS.items():
            self.order.addItem(text, key)
        tr.addWidget(self.template, 1)
        tr.addWidget(add)
        tr.addWidget(self.order)
        body.addWidget(self.template_row)
        self.replace_row = QWidget()
        rr = QGridLayout(self.replace_row)
        rr.setContentsMargins(0, 0, 0, 0)
        self.find = QLineEdit()
        self.replace_with = QLineEdit()
        rr.addWidget(_field("Existing text"), 0, 0)
        rr.addWidget(self.find, 0, 1)
        rr.addWidget(_field("Replace with"), 1, 0)
        rr.addWidget(self.replace_with, 1, 1)
        body.addWidget(self.replace_row)
        self.preview = QTreeWidget()
        self.preview.setColumnCount(2)
        self.preview.setHeaderLabels(["ORIGINAL NAME", "NEW NAME"])
        self.preview.setRootIsDecorated(False)
        self.preview.header().setStretchLastSection(True)
        self.preview.setColumnWidth(0, 360)
        body.addWidget(self.preview, 1)
        self.problem = QLabel("")
        self.problem.setProperty("role", "error")
        body.addWidget(self.problem)
        bb = QDialogButtonBox(QDialogButtonBox.Cancel)
        self.ok = bb.addButton("Rename", QDialogButtonBox.AcceptRole)
        self.ok.setDefault(True)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        body.addWidget(bb)
        for sig in (self.template.textChanged, self.find.textChanged, self.replace_with.textChanged,
                    self.order.currentIndexChanged, self.use_template.toggled):
            sig.connect(self._update)
        self.template.setFocus()
        self.template.selectAll()
        self._update()

    def items(self):
        order = self.order.currentData()
        if self.use_template.isChecked():
            return self.rename.from_template(self.paths, self.template.text(), order)
        return self.rename.find_replace(self.paths, self.find.text(), self.replace_with.text(), order)

    def _update(self, *_):
        from PySide6.QtWidgets import QTreeWidgetItem
        self.template_row.setVisible(self.use_template.isChecked())
        self.replace_row.setVisible(self.use_replace.isChecked())
        items = self.items()
        self.preview.clear()
        bad = 0
        for it in items:
            row = QTreeWidgetItem([it.name, it.new])
            if it.problem:
                bad += 1
                row.setForeground(1, self.QColor(theme.METER_RED))
                row.setToolTip(1, it.problem)
            elif it.new != it.name:
                row.setForeground(1, self.QColor(theme.LED_ORANGE))
            self.preview.addTopLevelItem(row)
        first = next((it.problem for it in items if it.problem), "")
        self.problem.setText("" if not bad else (first if bad == 1 else f"{bad} names can’t be used: {first}"))
        self.ok.setEnabled(self.rename.ready(items))
        self._items = items

    def accept(self):
        if self.ok.isEnabled():
            super().accept()

    def answer(self):
        return [(it.path, it.new) for it in self._items if it.new != it.name]


class PreferencesDialog(QDialog):
    """Files' Preferences: General (folders first, click to open, Delete Permanently in the menu) and Performance
    (search in subfolders, thumbnails, item counts: always / on this computer only / never)."""

    CHOICES = [("local", "On this computer only"), ("always", "Always"), ("never", "Never")]

    def __init__(self, prefs: dict, parent=None):
        super().__init__(parent)
        from PySide6.QtWidgets import QComboBox, QGroupBox
        self.setWindowTitle("Preferences")
        self.resize(620, 0)
        body = _shell(self, "PREFERENCES")
        general = QGroupBox("GENERAL")
        g = QGridLayout(general)
        self.folders_first = QCheckBox("Sort folders before files")
        self.folders_first.setChecked(prefs["folders_first"])
        g.addWidget(self.folders_first, 0, 0, 1, 2)
        g.addWidget(QLabel("Action to open items"), 1, 0)
        self.click = QComboBox()
        self.click.addItem("Double click", "double")
        self.click.addItem("Single click", "single")
        self.click.setCurrentIndex(self.click.findData(prefs["click"]))
        g.addWidget(self.click, 1, 1)
        self.show_delete = QCheckBox("Show action to permanently delete")
        self.show_delete.setChecked(prefs["show_delete"])
        g.addWidget(self.show_delete, 2, 0, 1, 2)
        self.expand_folders = QCheckBox("Expand folders in list view")
        self.expand_folders.setChecked(prefs.get("expand_folders", False))
        g.addWidget(self.expand_folders, 3, 0, 1, 2)
        g.setColumnStretch(0, 1)
        body.addWidget(general)
        perf = QGroupBox("PERFORMANCE")
        pg = QGridLayout(perf)
        self.search_subfolders = QCheckBox("Search in subfolders")
        self.search_subfolders.setChecked(prefs["search_subfolders"])
        pg.addWidget(self.search_subfolders, 0, 0, 1, 2)
        self.combos = {}
        for r, (key, label) in enumerate((("thumbnails", "Show thumbnails"),
                                          ("counts", "Count number of files in folders")), 1):
            pg.addWidget(QLabel(label), r, 0)
            c = QComboBox()
            for v, text in self.CHOICES:
                c.addItem(text, v)
            c.setCurrentIndex(c.findData(prefs[key]))
            pg.addWidget(c, r, 1)
            self.combos[key] = c
        hint = QLabel("“On this computer only” leaves out network drives, where reading every file is slow.")
        hint.setProperty("role", "dim")
        hint.setWordWrap(True)
        pg.addWidget(hint, 3, 0, 1, 2)
        pg.setColumnStretch(0, 1)
        body.addWidget(perf)
        bb = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        body.addWidget(bb)

    def answer(self) -> dict:
        return {"folders_first": self.folders_first.isChecked(), "click": self.click.currentData(),
                "show_delete": self.show_delete.isChecked(), "search_subfolders": self.search_subfolders.isChecked(),
                "expand_folders": self.expand_folders.isChecked(),
                "thumbnails": self.combos["thumbnails"].currentData(), "counts": self.combos["counts"].currentData()}


class LoginDialog(QDialog):
    """Files' "Authentication Required": registered user (name, password) or, for FTP, anonymous; remember the
    password (Windows Credential Manager) or not."""

    def __init__(self, host: str, user: str, protocol: str, parent=None):
        super().__init__(parent)
        from PySide6.QtWidgets import QRadioButton
        self.setWindowTitle("Authentication Required")
        self.resize(500, 0)
        body = _shell(self, "CONNECT TO SERVER")
        t = QLabel(f"Password required for \u201c{host}\u201d")
        t.setProperty("role", "heading")
        body.addWidget(t)
        row = QHBoxLayout()
        self.anon = QRadioButton("Connect anonymously")
        self.registered = QRadioButton("Registered user")
        self.registered.setChecked(True)
        row.addWidget(self.anon)
        row.addWidget(self.registered)
        row.addStretch(1)
        self.anon.setVisible(protocol == "ftp")
        self.registered.setVisible(protocol == "ftp")
        body.addLayout(row)
        g = QGridLayout()
        self.user = QLineEdit(user)
        self.password = QLineEdit()
        self.password.setEchoMode(QLineEdit.Password)
        g.addWidget(_field("Username"), 0, 0)
        g.addWidget(self.user, 0, 1)
        g.addWidget(_field("Password"), 1, 0)
        g.addWidget(self.password, 1, 1)
        body.addLayout(g)
        self.remember = QCheckBox("Remember password (in Windows Credential Manager)")
        body.addWidget(self.remember)
        bb = QDialogButtonBox(QDialogButtonBox.Cancel)
        ok = bb.addButton("Connect", QDialogButtonBox.AcceptRole)
        ok.setDefault(True)
        bb.accepted.connect(self.accept)
        bb.rejected.connect(self.reject)
        body.addWidget(bb)
        self.anon.toggled.connect(lambda on: [w.setEnabled(not on) for w in (self.user, self.password,
                                                                              self.remember)])
        (self.password if user else self.user).setFocus()

    def answer(self) -> dict:
        return {"anonymous": self.anon.isChecked(), "user": self.user.text().strip(),
                "password": self.password.text(), "remember": self.remember.isChecked()}
