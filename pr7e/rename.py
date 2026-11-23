"""Renaming several items at once, as GNOME Files' "Rename N Files" does it (no Qt): a template with tags, or find
and replace; the extension is kept; every new name is checked (empty, not allowed on Windows, the same as another
new name, taken by something else in the folder) before anything is renamed. The job renames in two steps,
through temporary names, so swaps (a -> b, b -> a) and chains never overwrite anything; Undo renames back."""

from __future__ import annotations

import os
import sys
import uuid
from dataclasses import dataclass

from . import fs, ops

TAG_NAME = "[Original file name]"
TAG_NUMBERS = {"[1, 2, 3]": "{}", "[01, 02, 03]": "{:02d}", "[001, 002, 003]": "{:03d}"}
TAGS = [TAG_NAME, *TAG_NUMBERS]
# Files' numbering orders.
ORDERS = {"name": "Original name (Ascending)", "name_desc": "Original name (Descending)",
          "mtime": "First modified", "mtime_desc": "Last modified"}


@dataclass
class Item:
    path: str
    new: str = ""          # the new full name ("" = unchanged)
    problem: str = ""      # why it can't be used ("" = fine)

    @property
    def name(self) -> str:
        return os.path.basename(self.path)


def ordered(paths: list[str], order: str = "name") -> list[str]:
    def mtime(p):
        try:
            return os.path.getmtime(p)
        except OSError:
            return 0.0
    by_name = sorted(paths, key=lambda p: fs.natural_key(os.path.basename(p)))
    if order == "name_desc":
        return list(reversed(by_name))
    if order == "mtime":
        return sorted(by_name, key=mtime)
    if order == "mtime_desc":
        return sorted(by_name, key=lambda p: -mtime(p))
    return by_name


def from_template(paths: list[str], template: str, order: str = "name") -> list[Item]:
    """Each item's stem becomes the template, with [Original file name] and a number put in."""
    items = []
    for i, p in enumerate(ordered(paths, order), 1):
        stem, ext = fs.split_ext(os.path.basename(p), os.path.isdir(p))
        new = template.replace(TAG_NAME, stem)
        for tag, fmt in TAG_NUMBERS.items():
            new = new.replace(tag, fmt.format(i))
        items.append(Item(p, new + ext))
    return check(items)


def find_replace(paths: list[str], find: str, replace: str, order: str = "name") -> list[Item]:
    """Every "find" in the stem becomes "replace" (case as typed, as in Files)."""
    items = []
    for p in ordered(paths, order):
        stem, ext = fs.split_ext(os.path.basename(p), os.path.isdir(p))
        items.append(Item(p, (stem.replace(find, replace) if find else stem) + ext))
    return check(items)


def _key(name: str) -> str:
    return name.lower() if sys.platform == "win32" else name


def check(items: list[Item]) -> list[Item]:
    """Mark the names that can't be used."""
    renaming = {os.path.normcase(i.path) for i in items}
    seen: dict[tuple[str, str], int] = {}
    for it in items:
        folder = os.path.dirname(it.path)
        k = (os.path.normcase(folder), _key(it.new))
        seen[k] = seen.get(k, 0) + 1
    for it in items:
        folder = os.path.dirname(it.path)
        it.problem = ""
        if not it.new.strip() or (it.new.startswith(".") and not it.name.startswith(".")):
            it.problem = "The name is empty."  # nothing left but the extension
        if not it.problem:
            p = fs.name_problem(it.new, folder, os.path.isdir(it.path), it.name)
            other = os.path.join(folder, it.new)
            # Taken by an item that's being renamed too is fine: it moves out of the way.
            if p.endswith("already exists.") and os.path.normcase(other) in renaming:
                p = ""
            it.problem = p
        if not it.problem and seen[(os.path.normcase(folder), _key(it.new))] > 1:
            it.problem = "Another file would get the same name."
    return items


def ready(items: list[Item]) -> bool:
    return any(it.new != it.name for it in items) and not any(it.problem for it in items)


class BatchRenameJob(ops.Job):
    kind = "rename"
    undo_label = "Rename"

    def __init__(self, pairs: list[tuple[str, str]]):
        """(path, new name) for each item that changes."""
        super().__init__()
        self.pairs = [(os.path.abspath(p), n) for p, n in pairs if os.path.basename(p) != n]
        self.title = f"Renaming {ops._files(len(self.pairs))}"
        self.total_items = len(self.pairs)
        self.renamed: list[tuple[str, str]] = []     # (new path, old path)

    def work(self):
        items = check([Item(p, n) for p, n in self.pairs])
        bad = [it for it in items if it.problem]
        if bad:
            raise OSError(0, bad[0].problem, bad[0].path)
        tag = uuid.uuid4().hex[:8]
        temps = []
        try:
            for p, n in self.pairs:  # step 1: out of each other's way
                tmp = os.path.join(os.path.dirname(p), f".pr7e-rename-{tag}-{len(temps)}")
                os.rename(p, tmp)
                temps.append((tmp, p, n))
        except OSError:
            for tmp, p, _n in reversed(temps):
                os.rename(tmp, p)
            raise
        for tmp, p, n in temps:      # step 2: the new names
            new = os.path.join(os.path.dirname(p), n)
            try:
                os.rename(tmp, new)
                self.renamed.append((new, p))
            except OSError as e:
                os.rename(tmp, p)
                self.errors.append(ops._error(e))
            self.done_items += 1
            self.changed.add(os.path.dirname(p))
        if self.renamed:
            self.undo = BatchRenameJob([(new, os.path.basename(old)) for new, old in self.renamed])
