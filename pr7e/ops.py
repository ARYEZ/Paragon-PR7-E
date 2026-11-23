"""File operations as GNOME Files does them (no Qt): copy, move, move to Trash, delete for good, rename, new
folder, restore from Trash, empty Trash. Each is a Job the window runs on a worker thread and watches (progress,
"N seconds left", cancel). A name that's taken asks Files' question (replace / merge / skip / rename, optionally
for all). Every job that can be undone leaves `undo`: the job that reverses it, whose own `undo` is the redo.
"""

from __future__ import annotations

import os
import shutil
import stat
import threading
import time
from dataclasses import dataclass

from . import fs, trash

CHUNK = 1 << 20


class Cancelled(Exception):
    pass


@dataclass
class Conflict:
    """What the "Replace file?" question shows."""
    source: str
    target: str
    source_dir: bool
    target_dir: bool
    source_size: int
    source_mtime: float
    target_size: int
    target_mtime: float

    @property
    def merge(self) -> bool:     # folder onto folder: Files offers Merge instead of Replace
        return self.source_dir and self.target_dir


def _conflict(src: str, target: str) -> Conflict:
    s, t = os.stat(src), os.stat(target)
    sd, td = stat.S_ISDIR(s.st_mode), stat.S_ISDIR(t.st_mode)
    return Conflict(src, target, sd, td, 0 if sd else s.st_size, s.st_mtime, 0 if td else t.st_size, t.st_mtime)


def _quote(path_or_name: str) -> str:
    return "“" + (os.path.basename(os.path.normpath(path_or_name)) or path_or_name) + "”"


def _files(n: int) -> str:
    return "1 file" if n == 1 else f"{n} files"


class Job:
    kind = ""
    undo_label = ""

    def __init__(self):
        self.title = ""
        self.ask = None                 # ask(Conflict) -> (action, apply_all) | ("rename", name) | None (cancel)
        self.total_bytes = 0
        self.done_bytes = 0
        self.total_items = 0
        self.done_items = 0
        self.state = "waiting"          # waiting, preparing, running, done, cancelled
        self.errors: list[str] = []
        self.undo: Job | None = None
        self.changed: set[str] = set()  # folders whose listing changes (the window reloads them)
        self.started = 0.0
        self._cancel = threading.Event()
        self._all: str | None = None    # the answer given "for all"

    # ------------------------------------------------------------------ what the window shows
    def cancel(self) -> None:
        self._cancel.set()

    @property
    def cancelled(self) -> bool:
        return self._cancel.is_set()

    def fraction(self) -> float:
        if self.total_bytes:
            return min(1.0, self.done_bytes / self.total_bytes)
        return min(1.0, self.done_items / self.total_items) if self.total_items else 0.0

    def detail(self) -> str:
        if self.state == "preparing":
            return "Preparing…"
        if self.total_bytes:
            text = f"{fs.human_size(self.done_bytes)} of {fs.human_size(self.total_bytes)}"
            took = time.monotonic() - self.started
            if took > 1 and self.done_bytes:
                left = (self.total_bytes - self.done_bytes) / (self.done_bytes / took)
                text += " — " + _left(left)
            return text
        return f"{self.done_items} of {_files(self.total_items)}"

    # ------------------------------------------------------------------ running
    def run(self) -> None:
        self.started = time.monotonic()
        try:
            self.state = "running"
            self.work()
            self.state = "cancelled" if self.cancelled else "done"
        except Cancelled:
            self.state = "cancelled"
        except OSError as e:
            self.errors.append(_error(e))
            self.state = "done"

    def work(self) -> None:
        raise NotImplementedError

    def check(self) -> None:
        if self._cancel.is_set():
            raise Cancelled

    def decide(self, src: str, target: str):
        """Files' question for a name that's taken. Returns "replace", "merge", "skip" or ("rename", name)."""
        c = _conflict(src, target)
        if self._all is not None:
            if self._all == "replace" and c.merge:
                return "merge"
            if self._all == "merge" and not c.merge:
                return "replace"
            return self._all
        if self.ask is None:
            return "merge" if c.merge else "replace"
        answer = self.ask(c)
        if answer is None:
            self.cancel()
            raise Cancelled
        action, extra = answer
        if action == "rename":
            return ("rename", extra)
        if extra:
            self._all = action
        return action


def _left(seconds: float) -> str:
    s = int(seconds + 0.5)
    if s < 60:
        return "1 second left" if s == 1 else f"{s} seconds left"
    m = (s + 30) // 60
    if m < 60:
        return "1 minute left" if m == 1 else f"{m} minutes left"
    h = (m + 30) // 60
    return "1 hour left" if h == 1 else f"{h} hours left"


def _error(e: OSError) -> str:
    what = e.filename and _quote(str(e.filename))
    reason = e.strerror or str(e)
    return f"{what}: {reason}" if what else reason


def _remove(path: str) -> None:
    def force(func, p, _exc):
        os.chmod(p, stat.S_IWRITE | stat.S_IREAD)
        func(p)
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path, onexc=force)
    else:
        try:
            os.remove(path)
        except PermissionError:
            force(os.remove, path, None)


# --------------------------------------------------------------------------- copy and move
class CopyJob(Job):
    """Copy (or move) files and folders into a folder. Pasting into the folder they're already in makes
    "name (copy)" (moving there does nothing). Same-drive moves are a rename: instant."""

    def __init__(self, sources: list[str], dest: str, move: bool = False):
        super().__init__()
        self.sources = [os.path.abspath(s) for s in sources]
        self.dest = os.path.abspath(dest)
        self.move = move
        self.kind = "move" if move else "copy"
        self.undo_label = "Move" if move else "Copy"
        verb = "Moving" if move else "Copying"
        what = _quote(self.sources[0]) if len(self.sources) == 1 else _files(len(self.sources))
        self.title = f"{verb} {what} to {_quote(self.dest)}"
        self.created: list[str] = []          # copy: the new top-level items (undo trashes them)
        self.moved: list[tuple[str, str]] = []  # move: (where it is now, where it was)

    def work(self):
        for s in self.sources:
            if os.path.isdir(s) and fs.is_inside(self.dest, s) and not (
                    self.move and os.path.normcase(os.path.dirname(s)) == os.path.normcase(self.dest)):
                verb = "move" if self.move else "copy"
                raise OSError(0, f"You cannot {verb} a folder into itself.", s)
        self.state = "preparing"
        moves_fast = self.move and all(fs.same_volume(s, self.dest) for s in self.sources)
        if moves_fast:
            self.total_items = len(self.sources)
        else:
            self.total_bytes, files, _dirs = fs.tree_size(self.sources, lambda: self.cancelled)
            self.total_items = files
        self.check()
        self.state = "running"
        self.changed.add(self.dest)
        for s in self.sources:
            self.check()
            try:
                if self.move:
                    self.changed.add(os.path.dirname(s))
                    self._move(s, self.dest, os.path.basename(s), fast=fs.same_volume(s, self.dest))
                else:
                    name = os.path.basename(s)
                    if os.path.normcase(os.path.dirname(s)) == os.path.normcase(self.dest):
                        name = fs.copy_name(name, self.dest, os.path.isdir(s))
                    made = self._copy(s, self.dest, name)
                    if made:
                        self.created.append(made)
            except Cancelled:
                raise
            except OSError as e:
                self.errors.append(_error(e))
        self._set_undo()

    def _set_undo(self):
        if self.move and self.moved:
            self.undo = MovePairsJob([(now, was) for now, was in reversed(self.moved)])
        elif not self.move and self.created:
            self.undo = TrashJob(self.created, label="Copy")

    def run(self):
        super().run()
        if self.state == "cancelled":
            self._set_undo()   # what was done before the cancel can still be undone

    # ---------------------------------------------------------------- copying
    def _target(self, src: str, folder: str, name: str):
        """(target path, merging?) or None to skip."""
        target = os.path.join(folder, name)
        if not os.path.lexists(target):
            return target, False
        if os.path.normcase(os.path.abspath(src)) == os.path.normcase(os.path.abspath(target)):
            return None
        action = self.decide(src, target)
        if action == "skip":
            return None
        if isinstance(action, tuple):
            return self._target(src, folder, action[1])
        if action == "merge" and os.path.isdir(target) and os.path.isdir(src):
            return target, True
        _remove(target)  # replace
        return target, False

    def _copy(self, src: str, folder: str, name: str) -> str | None:
        got = self._target(src, folder, name)
        if got is None:
            if not os.path.isdir(src):
                self.done_bytes += os.path.getsize(src)
                self.done_items += 1
            return None
        target, merging = got
        if os.path.isdir(src) and not os.path.islink(src):
            if not merging:
                os.makedirs(target)
            for child in sorted(os.listdir(src)):
                self.check()
                try:
                    self._copy(os.path.join(src, child), target, child)
                except Cancelled:
                    raise
                except OSError as e:
                    self.errors.append(_error(e))
            try:
                shutil.copystat(src, target)
            except OSError:
                pass
            return None if merging else target
        self._copy_file(src, target)
        return target

    def _copy_file(self, src: str, target: str) -> None:
        try:
            with open(src, "rb") as fi, open(target, "wb") as fo:
                while True:
                    self.check()
                    buf = fi.read(CHUNK)
                    if not buf:
                        break
                    fo.write(buf)
                    self.done_bytes += len(buf)
        except BaseException:
            try:
                os.remove(target)  # no half-copied files left behind
            except OSError:
                pass
            raise
        shutil.copystat(src, target)
        self.done_items += 1

    # ---------------------------------------------------------------- moving
    def _move(self, src: str, folder: str, name: str, fast: bool) -> None:
        target = os.path.join(folder, name)
        if os.path.normcase(os.path.abspath(src)) == os.path.normcase(os.path.abspath(target)):
            self.done_items += 1
            return
        if os.path.lexists(target):
            if os.path.normcase(src) != os.path.normcase(target) and os.path.normcase(src).lower() == \
                    os.path.normcase(target).lower() and os.path.samefile(src, target):
                pass  # only the case differs on a case-insensitive drive: a plain rename
            else:
                action = self.decide(src, target)
                if action == "skip":
                    self._count_skipped(src, fast)
                    return
                if isinstance(action, tuple):
                    return self._move(src, folder, action[1], fast)
                if action == "merge" and os.path.isdir(target) and os.path.isdir(src):
                    for child in sorted(os.listdir(src)):
                        self.check()
                        self._move(os.path.join(src, child), target, child, fast)
                    try:
                        os.rmdir(src)
                    except OSError:
                        pass  # something was skipped: the folder stays
                    return
                _remove(target)
        if fast:
            os.rename(src, target)
            self.done_items += 1
        else:
            if os.path.isdir(src) and not os.path.islink(src):
                os.makedirs(target)
                for child in sorted(os.listdir(src)):
                    self.check()
                    self._copy(os.path.join(src, child), target, child)
                shutil.copystat(src, target)
            else:
                self._copy_file(src, target)
            _remove(src)
        self.moved.append((target, src))

    def _count_skipped(self, src, fast):
        if fast:
            self.done_items += 1
        else:
            b, f, _ = fs.tree_size([src])
            self.done_bytes += b
            self.done_items += f


class MovePairsJob(Job):
    """Put moved things back (undo of a move), each to its own place."""
    kind = "move"
    undo_label = "Move"

    def __init__(self, pairs: list[tuple[str, str]]):
        super().__init__()
        self.pairs = pairs
        self.title = f"Moving {_quote(pairs[0][0]) if len(pairs) == 1 else _files(len(pairs))} back"
        self.total_items = len(pairs)

    def work(self):
        done = []
        for now, was in self.pairs:
            self.check()
            try:
                os.makedirs(os.path.dirname(was), exist_ok=True)
                if os.path.lexists(was):
                    raise FileExistsError(0, "something else has its name now", was)
                if fs.same_volume(now, os.path.dirname(was)):
                    os.rename(now, was)
                else:
                    shutil.move(now, was)
                done.append((was, now))
                self.changed.update((os.path.dirname(now), os.path.dirname(was)))
            except OSError as e:
                self.errors.append(_error(e))
            self.done_items += 1
        if done:
            self.undo = MovePairsJob(list(reversed(done)))


# --------------------------------------------------------------------------- Trash
class TrashJob(Job):
    """Move to Trash (Delete). Items that can't go to Trash (no Recycle Bin on that drive) are kept in
    `untrashable`: the window asks whether to delete them for good, as Files does."""
    kind = "trash"
    undo_label = "Move to Trash"

    def __init__(self, paths: list[str], label: str = ""):
        super().__init__()
        self.paths = [os.path.abspath(p) for p in paths]
        if label:
            self.undo_label = label
        self.title = f"Moving {_quote(self.paths[0]) if len(self.paths) == 1 else _files(len(self.paths))} to Trash"
        self.total_items = len(self.paths)
        self.items: list[trash.TrashItem] = []
        self.untrashable: list[str] = []

    def work(self):
        for p in self.paths:
            self.check()
            try:
                self.items.append(trash.send(p))
            except trash.TrashError as e:
                if os.path.lexists(p):
                    self.untrashable.append(p)
                else:
                    self.errors.append(str(e))
            except OSError as e:
                self.untrashable.append(p) if os.path.lexists(p) else self.errors.append(_error(e))
            self.changed.add(os.path.dirname(p))
            self.done_items += 1
        if self.items:
            self.undo = RestoreJob(self.items, label=self.undo_label)


class RestoreJob(Job):
    """Restore from Trash (and undo of Move to Trash)."""
    kind = "restore"
    undo_label = "Restore"

    def __init__(self, items: list[trash.TrashItem], label: str = ""):
        super().__init__()
        self.items = items
        if label:
            self.undo_label = label
        self.title = f"Restoring {_quote(items[0].name) if len(items) == 1 else _files(len(items))} from Trash"
        self.total_items = len(items)
        self.restored: list[str] = []

    def work(self):
        for it in self.items:
            self.check()
            try:
                self.restored.append(trash.restore(it))
                self.changed.add(os.path.dirname(it.original))
            except OSError as e:
                self.errors.append(_error(e))
            self.done_items += 1
        if self.restored:
            self.undo = TrashJob(self.restored, label=self.undo_label)


class DeleteJob(Job):
    """Delete for good (Shift+Delete, after the question): no undo."""
    kind = "delete"

    def __init__(self, paths: list[str]):
        super().__init__()
        self.paths = [os.path.abspath(p) for p in paths]
        self.title = f"Deleting {_quote(self.paths[0]) if len(self.paths) == 1 else _files(len(self.paths))}"
        self.total_items = len(self.paths)

    def work(self):
        for p in self.paths:
            self.check()
            try:
                _remove(p)
                self.changed.add(os.path.dirname(p))
            except OSError as e:
                self.errors.append(_error(e))
            self.done_items += 1


class TrashDeleteJob(Job):
    """Delete items in Trash for good; with items=None, empty the Trash."""
    kind = "delete"

    def __init__(self, items: list[trash.TrashItem] | None = None):
        super().__init__()
        self.items = items
        self.title = "Emptying Trash" if items is None else (
            f"Deleting {_quote(items[0].name) if len(items) == 1 else _files(len(items))} from Trash")
        self.total_items = 1 if items is None else len(items)

    def work(self):
        if self.items is None:
            trash.empty()
            self.done_items = 1
            return
        for it in self.items:
            self.check()
            try:
                trash.delete(it)
            except OSError as e:
                self.errors.append(_error(e))
            self.done_items += 1


# --------------------------------------------------------------------------- rename, new folder
class RenameJob(Job):
    kind = "rename"
    undo_label = "Rename"

    def __init__(self, path: str, new_name: str):
        super().__init__()
        self.path = os.path.abspath(path)
        self.new_name = new_name
        self.title = f"Renaming {_quote(self.path)} as {_quote(new_name)}"
        self.total_items = 1
        self.new_path = os.path.join(os.path.dirname(self.path), new_name)

    def work(self):
        folder = os.path.dirname(self.path)
        problem = fs.name_problem(self.new_name, folder, os.path.isdir(self.path), os.path.basename(self.path))
        if problem or not self.new_name.strip():
            raise OSError(0, problem or "The name is empty.", self.path)
        os.rename(self.path, self.new_path)
        self.changed.add(folder)
        self.done_items = 1
        self.undo = RenameJob(self.new_path, os.path.basename(self.path))


class NewFolderJob(Job):
    kind = "mkdir"
    undo_label = "Create Folder"

    def __init__(self, folder: str, name: str):
        super().__init__()
        self.path = os.path.join(os.path.abspath(folder), name)
        self.title = f"Creating {_quote(name)}"
        self.total_items = 1

    def work(self):
        os.mkdir(self.path)
        self.changed.add(os.path.dirname(self.path))
        self.done_items = 1
        self.undo = TrashJob([self.path], label="Create Folder")
