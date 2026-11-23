"""PR7-E's dialogs, in GNOME Files' words: rename / new folder, the "Replace file?" question, confirmations,
Properties, keyboard shortcuts and About. Each answers through answer() so the window's `answers` can stand in
for it in tests."""

from __future__ import annotations

import os
import threading

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QPixmap
from PySide6.QtWidgets import (QCheckBox, QDialog, QDialogButtonBox, QGridLayout, QHBoxLayout, QLabel, QLineEdit,
                               QPushButton, QVBoxLayout)

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
                 current: str = "", parent=None):
        super().__init__(parent)
        self.setWindowTitle(title)
        self.folder, self.is_dir, self.current = folder, is_dir, current
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

    def __init__(self, entries: list[fs.Entry], parent=None, thumb: QPixmap | None = None):
        super().__init__(parent)
        self.entries = entries
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
        folder = os.path.dirname(entries[0].path)
        if one:
            self._row("Type", system.type_name(one.path, one.is_dir))
        else:
            self._row("Contents", fs.items_text(len(entries)))
        self._row("Parent folder", folder)
        if one:
            try:
                st = os.stat(one.path)
                self._row("Modified", fs.full_date(st.st_mtime))
                self._row("Accessed", fs.full_date(st.st_atime))
                created = getattr(st, "st_birthtime", None) or (st.st_ctime if os.name == "nt" else None)
                if created:
                    self._row("Created", fs.full_date(created))
            except OSError:
                pass
        d = places.drive_of(entries[0].path)
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
