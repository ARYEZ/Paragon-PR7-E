"""Trash: Windows' Recycle Bin (send, list, restore, delete, empty), or a freedesktop.org trash folder (Linux,
and the tests everywhere: PR7E_TRASH names its folder, so tests never touch the real Recycle Bin). No Qt.

Windows keeps each recycled item as two files in <drive>:\\$Recycle.Bin\\<your SID>\\: $R<id> (the file or folder
itself) and $I<id> (its original path, size and when it was deleted). Sending uses SHFileOperationW with
FOF_ALLOWUNDO, the same call Explorer makes, so the items appear in Explorer's Recycle Bin too.
"""

from __future__ import annotations

import os
import shutil
import struct
import sys
import time
import urllib.parse
from dataclasses import dataclass

from . import fs


class TrashError(OSError):
    pass


@dataclass
class TrashItem:
    name: str
    original: str          # the full path it was deleted from
    deleted: float         # when
    size: int
    is_dir: bool
    data: str              # where the item itself is now
    info: str              # its record ($I file or .trashinfo)


# --------------------------------------------------------------------------- freedesktop.org trash folder
class FolderTrash:
    """https://specifications.freedesktop.org/trash-spec/: files/<name> and info/<name>.trashinfo."""

    def __init__(self, root: str):
        self.root = root
        self.files = os.path.join(root, "files")
        self.infos = os.path.join(root, "info")

    def send(self, path: str) -> TrashItem:
        os.makedirs(self.files, exist_ok=True)
        os.makedirs(self.infos, exist_ok=True)
        path = os.path.abspath(path)
        name = os.path.basename(path)
        stem, ext = fs.split_ext(name, os.path.isdir(path))
        n, cand = 1, name
        while True:  # O_EXCL on the info file claims the name, as the spec asks
            try:
                fd = os.open(os.path.join(self.infos, cand + ".trashinfo"), os.O_WRONLY | os.O_CREAT | os.O_EXCL)
                break
            except FileExistsError:
                n += 1
                cand = f"{stem}.{n}{ext}"
        now = time.time()
        with os.fdopen(fd, "w", encoding="utf-8") as fh:
            fh.write("[Trash Info]\nPath=%s\nDeletionDate=%s\n" % (
                urllib.parse.quote(path.replace("\\", "/") if sys.platform == "win32" else path),
                time.strftime("%Y-%m-%dT%H:%M:%S", time.localtime(now))))
        info = os.path.join(self.infos, cand + ".trashinfo")
        data = os.path.join(self.files, cand)
        try:
            shutil.move(path, data)
        except OSError:
            os.remove(info)
            raise
        return self._item(info)

    def _item(self, info: str) -> TrashItem | None:
        try:
            with open(info, encoding="utf-8") as fh:
                lines = dict(line.rstrip("\n").split("=", 1) for line in fh if "=" in line)
        except (OSError, ValueError):
            return None
        original = urllib.parse.unquote(lines.get("Path", ""))
        if sys.platform == "win32":
            original = original.replace("/", "\\")
        try:
            deleted = time.mktime(time.strptime(lines.get("DeletionDate", "")[:19], "%Y-%m-%dT%H:%M:%S"))
        except ValueError:
            deleted = 0.0
        data = os.path.join(self.files, os.path.basename(info)[:-len(".trashinfo")])
        if not os.path.lexists(data):
            return None
        d = os.path.isdir(data)
        size = fs.tree_size([data])[0] if d else os.path.getsize(data)
        return TrashItem(os.path.basename(original) or os.path.basename(data), original, deleted, size, d, data, info)

    def items(self) -> list[TrashItem]:
        try:
            names = sorted(os.listdir(self.infos))
        except OSError:
            return []
        out = [self._item(os.path.join(self.infos, n)) for n in names if n.endswith(".trashinfo")]
        return [i for i in out if i is not None]

    def forget(self, item: TrashItem) -> None:
        try:
            os.remove(item.info)
        except OSError:
            pass

    def empty(self) -> None:
        for it in self.items():
            delete(it, self)


# --------------------------------------------------------------------------- Windows' Recycle Bin
def _parse_i_file(data: bytes) -> tuple[str, float, int] | None:
    """A $I file: version (8), size (8), deletion FILETIME (8), then the path: version 1 = 260 UTF-16 chars,
    version 2 = a 4-byte length and that many UTF-16 chars."""
    if len(data) < 24:
        return None
    version, size, filetime = struct.unpack_from("<qqq", data, 0)
    if version == 1:
        raw = data[24:24 + 520]
    elif version == 2 and len(data) >= 28:
        n = struct.unpack_from("<i", data, 24)[0]
        raw = data[28:28 + 2 * n]
    else:
        return None
    path = raw.decode("utf-16-le", "replace").split("\x00", 1)[0]
    deleted = filetime / 1e7 - 11644473600 if filetime else 0.0
    return path, deleted, size


def make_i_file(path: str, size: int, deleted: float) -> bytes:
    """The other way round (tests)."""
    ft = int((deleted + 11644473600) * 1e7)
    text = path + "\x00"
    return struct.pack("<qqqi", 2, size, ft, len(text)) + text.encode("utf-16-le")


class WindowsTrash:
    def __init__(self, bins: list[str] | None = None):
        self._bins = bins  # tests pass folders laid out like $Recycle.Bin\<SID>

    def bins(self) -> list[str]:
        """Your own folder in each drive's $Recycle.Bin (the others belong to other users and can't be read)."""
        if self._bins is not None:
            return self._bins
        out = []
        import string
        import ctypes
        mask = ctypes.windll.kernel32.GetLogicalDrives()
        for i, letter in enumerate(string.ascii_uppercase):
            if not mask & (1 << i):
                continue
            root = f"{letter}:\\$Recycle.Bin"
            try:
                for sid in os.listdir(root):
                    p = os.path.join(root, sid)
                    try:
                        os.listdir(p)
                        out.append(p)
                    except OSError:
                        pass
            except OSError:
                pass
        return out

    def send(self, path: str) -> TrashItem:
        import ctypes
        from ctypes import wintypes

        class SHFILEOPSTRUCTW(ctypes.Structure):
            _fields_ = [("hwnd", wintypes.HWND), ("wFunc", wintypes.UINT), ("pFrom", wintypes.LPCWSTR),
                        ("pTo", wintypes.LPCWSTR), ("fFlags", ctypes.c_uint16), ("fAnyOperationsAborted", wintypes.BOOL),
                        ("hNameMappings", ctypes.c_void_p), ("lpszProgressTitle", wintypes.LPCWSTR)]
        FO_DELETE, FOF_SILENT, FOF_NOCONFIRMATION, FOF_ALLOWUNDO, FOF_NOERRORUI, FOF_WANTNUKEWARNING = \
            3, 0x4, 0x10, 0x40, 0x400, 0x4000
        path = os.path.abspath(path)
        before = time.time() - 2
        op = SHFILEOPSTRUCTW(None, FO_DELETE, path + "\x00", None,
                             FOF_SILENT | FOF_NOCONFIRMATION | FOF_ALLOWUNDO | FOF_NOERRORUI | FOF_WANTNUKEWARNING,
                             False, None, None)
        res = ctypes.windll.shell32.SHFileOperationW(ctypes.byref(op))
        if res or op.fAnyOperationsAborted or os.path.lexists(path):
            raise TrashError(f"“{os.path.basename(path)}” can’t be put in the Trash")
        found = [i for i in self.items() if os.path.normcase(i.original) == os.path.normcase(path)
                 and i.deleted >= before]
        if not found:  # the drive has no Recycle Bin (network drives): Windows deleted it for good
            raise TrashError(f"“{os.path.basename(path)}” was deleted: its drive has no Trash")
        return max(found, key=lambda i: i.deleted)

    def items(self) -> list[TrashItem]:
        out = []
        for b in self.bins():
            try:
                names = os.listdir(b)
            except OSError:
                continue
            for n in names:
                if not n.upper().startswith("$I"):
                    continue
                info = os.path.join(b, n)
                data = os.path.join(b, "$R" + n[2:])
                if not os.path.lexists(data):
                    continue
                try:
                    with open(info, "rb") as fh:
                        parsed = _parse_i_file(fh.read())
                except OSError:
                    continue
                if not parsed:
                    continue
                original, deleted, size = parsed
                out.append(TrashItem(original.rsplit("\\", 1)[-1], original, deleted, size, os.path.isdir(data),
                                     data, info))
        return out

    def forget(self, item: TrashItem) -> None:
        try:
            os.remove(item.info)
        except OSError:
            pass

    def empty(self) -> None:
        if self._bins is None:
            import ctypes
            SHERB_NOCONFIRMATION, SHERB_NOPROGRESSUI, SHERB_NOSOUND = 1, 2, 4
            ctypes.windll.shell32.SHEmptyRecycleBinW(None, None, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI |
                                                     SHERB_NOSOUND)
            return
        for it in self.items():
            delete(it, self)


# --------------------------------------------------------------------------- the rest of PR7-E uses these
_backend = None


def backend():
    global _backend
    folder = os.environ.get("PR7E_TRASH")
    if folder:
        if not isinstance(_backend, FolderTrash) or _backend.root != folder:
            _backend = FolderTrash(folder)
    elif _backend is None:
        if sys.platform == "win32":
            _backend = WindowsTrash()
        else:
            data = os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share")
            _backend = FolderTrash(os.path.join(data, "Trash"))
    return _backend


def send(path: str, b=None) -> TrashItem:
    return (b or backend()).send(path)


def items(b=None) -> list[TrashItem]:
    return sorted((b or backend()).items(), key=lambda i: -i.deleted)


def restore(item: TrashItem, b=None, target: str | None = None) -> str:
    """Put an item back where it came from (or at `target`); the folder is made again if it's gone. Returns
    where it went. A name that's taken now gets " (2)" added, so nothing is overwritten."""
    b = b or backend()
    dest = target or item.original
    folder = os.path.dirname(dest)
    os.makedirs(folder, exist_ok=True)
    if target is None:
        dest = os.path.join(folder, fs.free_name(os.path.basename(dest), folder, item.is_dir))
    shutil.move(item.data, dest)
    b.forget(item)
    return dest


def delete(item: TrashItem, b=None) -> None:
    """Delete one item from Trash for good."""
    b = b or backend()
    if item.is_dir and not os.path.islink(item.data):
        shutil.rmtree(item.data, onexc=_force)
    else:
        try:
            os.remove(item.data)
        except PermissionError:
            _force(os.remove, item.data, None)
    b.forget(item)


def empty(b=None) -> None:
    (b or backend()).empty()


def _force(func, path, _exc):
    """Read-only files (common on Windows) are made writable and tried again."""
    import stat
    os.chmod(path, stat.S_IWRITE | stat.S_IREAD)
    func(path)
