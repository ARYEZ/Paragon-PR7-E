"""Reading folders the way GNOME Files shows them (no Qt here): entries, Files' sort orders (folders first,
natural name order), hidden files, file kinds, the search with its When / What filters, and Files' wording for
sizes and dates."""

from __future__ import annotations

import os
import re
import stat
import sys
import time
import unicodedata
from dataclasses import dataclass, field

WINDOWS = sys.platform == "win32"


@dataclass
class Entry:
    name: str
    path: str
    is_dir: bool = False
    size: int = 0              # bytes; for folders the number of items in them, -1 until counted
    mtime: float = 0.0
    hidden: bool = False
    location: str = ""         # search results, Recent, Starred, Trash: the folder it's in (shown in LOCATION)
    kind: str = ""
    trash: object = None       # a trash.TrashItem for entries in Trash
    extra: str = ""            # Other Locations: "120.3 GB free of 512.1 GB"
    depth: int = 0             # list view with expanded folders: how far in (0 = the folder shown)

    def __post_init__(self):
        self.kind = self.kind or kind_of(self.name, self.is_dir)


# --------------------------------------------------------------------------- kinds of file
KINDS = {
    "audio": "mp3 flac m4a ogg opus wav wma aac aiff ape",
    "video": "mp4 mkv avi mov webm wmv m4v mpg mpeg ts",
    "image": "jpg jpeg png gif webp bmp tif tiff heic svg ico avif",
    "archive": "zip 7z rar tar gz tgz xz txz bz2 zst iso cab",
    "text": "txt md log ini cfg conf json xml csv py js ts c cpp h cs java go rs bat ps1 sh yml yaml toml html css",
    "document": "pdf doc docx odt xls xlsx ods ppt pptx odp rtf epub",
}
_EXT_KIND = {ext: kind for kind, exts in KINDS.items() for ext in exts.split()}
IMAGE_EXTS = set(KINDS["image"].split()) - {"svg", "heic", "avif"}

# Files' "What" search filter, in its order.
WHAT = {"Folders": "folder", "Documents": "document", "Music": "audio", "Pictures": "image", "Videos": "video",
        "Text": "text", "Archives": "archive"}
# The "When" filter: a label and how many days back.
WHEN = {"Today": 1, "Last 7 days": 7, "Last 30 days": 30, "Last year": 365}


def kind_of(name: str, is_dir: bool = False) -> str:
    if is_dir:
        return "folder"
    ext = name.rsplit(".", 1)[-1].lower() if "." in name else ""
    return _EXT_KIND.get(ext, "file")


# --------------------------------------------------------------------------- listing
def is_hidden(name: str, st: os.stat_result | None) -> bool:
    """Hidden as Files shows it on Windows: dot files, and files with Windows' hidden or system attribute."""
    if name.startswith("."):
        return True
    attrs = getattr(st, "st_file_attributes", 0) if st is not None else 0
    return bool(attrs & (stat.FILE_ATTRIBUTE_HIDDEN | stat.FILE_ATTRIBUTE_SYSTEM)) if WINDOWS else False


def entry_for(path: str, name: str | None = None) -> Entry:
    st = os.stat(path)
    name = name or os.path.basename(os.path.normpath(path)) or path
    d = stat.S_ISDIR(st.st_mode)
    return Entry(name, path, d, -1 if d else st.st_size, st.st_mtime, is_hidden(name, st))


def list_dir(path: str, show_hidden: bool = False) -> list[Entry]:
    """The folder's entries (OSError if it can't be read). Broken links and files that vanish are left out."""
    out = []
    with os.scandir(path) as it:
        for de in it:
            try:
                st = de.stat()
            except OSError:
                continue
            d = stat.S_ISDIR(st.st_mode)
            e = Entry(de.name, de.path, d, -1 if d else st.st_size, st.st_mtime, is_hidden(de.name, st))
            if show_hidden or not e.hidden:
                out.append(e)
    return out


def count_items(path: str, show_hidden: bool = False) -> int:
    """What Files shows as a folder's size: how many items are in it (-1 if it can't be read)."""
    try:
        return len(list_dir(path, show_hidden))
    except OSError:
        return -1


# --------------------------------------------------------------------------- sorting
_DIGITS = re.compile(r"(\d+)")


def natural_key(name: str):
    """Files' name order: case- and accent-insensitive, numbers by value (2 before 10)."""
    folded = fold(name)
    return [(0, int(part), "") if part.isdigit() else (1, 0, part) for part in _DIGITS.split(folded) if part]


def fold(text: str) -> str:
    """Lower case without accents, for sorting and searching ("Café" matches "cafe")."""
    return "".join(c for c in unicodedata.normalize("NFKD", text.casefold()) if not unicodedata.combining(c))


SORTS = {"name": "A-Z", "name_desc": "Z-A", "mtime_desc": "Last modified", "mtime": "First modified",
         "size_desc": "Size", "type": "Type"}


def sort_entries(entries: list[Entry], key: str = "name", folders_first: bool = True) -> list[Entry]:
    """Files' sort menu. Folders stay before files in every order (unless the preference is off)."""
    by_name = sorted(entries, key=lambda e: natural_key(e.name))
    if key == "name_desc":
        by_name.reverse()
    elif key == "mtime_desc":
        by_name.sort(key=lambda e: -e.mtime)
    elif key == "mtime":
        by_name.sort(key=lambda e: e.mtime)
    elif key == "size_desc":
        by_name.sort(key=lambda e: -e.size)
    elif key == "size":  # the list's SIZE column clicked twice: smallest first
        by_name.sort(key=lambda e: e.size)
    elif key == "type":
        by_name.sort(key=lambda e: (e.kind, e.name.rsplit(".", 1)[-1].lower() if "." in e.name else ""))
    if not folders_first:
        return by_name
    return [e for e in by_name if e.is_dir] + [e for e in by_name if not e.is_dir]


# --------------------------------------------------------------------------- search
def matches(name: str, words: list[str]) -> bool:
    f = fold(name)
    return all(w in f for w in words)


def search_filter(text: str, when: str = "", what: str = "", now: float | None = None):
    """Files' search rules as a test on an entry: every word typed in its name (any case, accents ignored),
    modified within When, of the kind What. None when there's nothing to look for. (Server search uses it too.)"""
    words = [fold(w) for w in text.split()]
    if not words and not when and not what:
        return None
    cutoff = 0.0
    if when in WHEN:
        now = now or time.time()
        if WHEN[when] == 1:
            lt = time.localtime(now)
            cutoff = time.mktime((lt.tm_year, lt.tm_mon, lt.tm_mday, 0, 0, 0, 0, 0, -1))
        else:
            cutoff = now - WHEN[when] * 86400
    want = WHAT.get(what, "")

    def ok(e: Entry) -> bool:
        return matches(e.name, words) and not (cutoff and e.mtime < cutoff) and not (want and e.kind != want)
    return ok


def search(root: str, text: str, when: str = "", what: str = "", show_hidden: bool = False,
           cancelled=lambda: False, now: float | None = None, recursive: bool = True):
    """Files' search: everything under `root` that passes `search_filter`. Yields entries as they're found, with
    LOCATION relative to `root`. Folders that can't be read are passed over."""
    ok = search_filter(text, when, what, now)
    if ok is None:
        return
    stack = [root]
    while stack:
        if cancelled():
            return
        folder = stack.pop()
        try:
            entries = list_dir(folder, show_hidden)
        except OSError:
            continue
        rel = os.path.relpath(folder, root)
        rel = "" if rel == "." else rel.replace("\\", "/")
        for e in sort_entries(entries):
            if recursive and e.is_dir and not os.path.islink(e.path):
                stack.append(e.path)
            if ok(e):
                e.location = rel or os.path.basename(os.path.normpath(root)) or root
                yield e


# --------------------------------------------------------------------------- Files' wording
def human_size(n: int) -> str:
    """Files' way: decimal units, one decimal place (4.2 MB, 1.2 GB)."""
    if n < 1000:
        return "1 byte" if n == 1 else f"{n} bytes"
    for unit in ("kB", "MB", "GB", "TB", "PB"):
        n /= 1000
        if n < 999.95 or unit == "PB":
            return f"{n:.1f} {unit}"
    return ""


def items_text(n: int) -> str:
    if n < 0:
        return ""
    return "1 item" if n == 1 else f"{n} items"


def size_text(e: Entry) -> str:
    if e.kind in ("drive", "usb"):
        return e.extra
    return items_text(e.size) if e.is_dir else human_size(e.size)


def date_text(t: float, now: float | None = None) -> str:
    """Files' Modified column: the time for today, "Yesterday", the weekday this week, else the date."""
    if not t:
        return ""
    now = now or time.time()
    day = lambda x: time.mktime(time.localtime(x)[:3] + (0, 0, 0, 0, 0, -1))  # noqa: E731
    days = round((day(now) - day(t)) / 86400)
    lt = time.localtime(t)
    if days <= 0:
        return time.strftime("%H:%M", lt)
    if days == 1:
        return "Yesterday"
    if days < 7:
        return time.strftime("%A", lt)
    return f"{lt.tm_mday} {time.strftime('%b %Y', lt)}"


def full_date(t: float) -> str:
    """Properties: "Monday 23 November 2026, 14:05"."""
    lt = time.localtime(t)
    return f"{time.strftime('%A', lt)} {lt.tm_mday} {time.strftime('%B %Y, %H:%M', lt)}"


# --------------------------------------------------------------------------- names
BAD_CHARS = set('<>:"/\\|?*') if WINDOWS else {"/"}
RESERVED = {"CON", "PRN", "AUX", "NUL", *(f"COM{i}" for i in range(1, 10)), *(f"LPT{i}" for i in range(1, 10))}


def name_problem(name: str, folder: str, is_dir: bool = False, current: str = "") -> str:
    """Why a new name can't be used, in Files' words ("" when it can). Windows' own rules are checked on every
    system, so names made here work when the files go to Windows."""
    what = "Folder" if is_dir else "File"
    if not name.strip():
        return ""  # an empty name just leaves the button off, as in Files
    if name in (".", ".."):
        return f"A {what.lower()} cannot be called “{name}”."
    if any(c in set('<>:"/\\|?*') for c in name) or any(ord(c) < 32 for c in name):
        return f"{what} names cannot contain < > : \" / \\ | ? *"
    if name.rstrip(" .") != name:
        return f"{what} names cannot end with a space or a full stop."
    if name.split(".")[0].upper() in RESERVED:
        return f"“{name}” is a name Windows keeps for itself."
    if name != current and os.path.lexists(os.path.join(folder, name)):
        if current and name.lower() == current.lower() and WINDOWS:
            return ""  # changing only the case of a name is fine
        return f"A {'folder' if os.path.isdir(os.path.join(folder, name)) else 'file'} with that name already exists."
    return ""


def split_ext(name: str, is_dir: bool = False) -> tuple[str, str]:
    """("photo", ".jpg"); folders, dot files and ".tar.gz"-style names as Files splits them."""
    if is_dir or name.startswith(".") and name.count(".") == 1:
        return name, ""
    low = name.lower()
    for double in (".tar.gz", ".tar.xz", ".tar.bz2", ".tar.zst"):
        if low.endswith(double) and len(name) > len(double):
            return name[:-len(double)], name[-len(double):]
    stem, ext = os.path.splitext(name)
    return (stem, ext) if stem else (name, "")


def copy_name(name: str, folder: str, is_dir: bool = False) -> str:
    """Files' name for a copy in the same folder: "photo (copy).jpg", then "photo (copy 2).jpg"..."""
    stem, ext = split_ext(name, is_dir)
    m = re.fullmatch(r"(.*) \(copy(?: (\d+))?\)", stem)
    base, n = (m.group(1), int(m.group(2) or 1)) if m else (stem, 0)
    while True:
        n += 1
        cand = f"{base} (copy){ext}" if n == 1 else f"{base} (copy {n}){ext}"
        if not os.path.lexists(os.path.join(folder, cand)):
            return cand


def free_name(name: str, folder: str, is_dir: bool = False) -> str:
    """A name that isn't taken yet: "name (2).ext", "name (3).ext"... (restoring from Trash, conflicts)."""
    if not os.path.lexists(os.path.join(folder, name)):
        return name
    stem, ext = split_ext(name, is_dir)
    n = 2
    while os.path.lexists(os.path.join(folder, f"{stem} ({n}){ext}")):
        n += 1
    return f"{stem} ({n}){ext}"


def is_inside(path: str, folder: str) -> bool:
    p, f = os.path.normcase(os.path.abspath(path)), os.path.normcase(os.path.abspath(folder))
    return p == f or p.startswith(f.rstrip("\\/") + os.sep)


def same_volume(a: str, b: str) -> bool:
    try:
        return os.stat(a).st_dev == os.stat(b).st_dev
    except OSError:
        return False


def tree_size(paths: list[str], cancelled=lambda: False, progress=None) -> tuple[int, int, int]:
    """(bytes, files, folders) under the given paths, for Properties and for planning copies."""
    total = files = folders = 0
    stack = list(paths)
    while stack:
        if cancelled():
            break
        p = stack.pop()
        try:
            st = os.stat(p, follow_symlinks=False)
        except OSError:
            continue
        if stat.S_ISDIR(st.st_mode):
            folders += 1
            try:
                with os.scandir(p) as it:
                    stack.extend(de.path for de in it)
            except OSError:
                pass
        else:
            files += 1
            total += st.st_size
        if progress and (files + folders) % 200 == 0:
            progress(total, files, folders)
    return total, files, folders
