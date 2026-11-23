"""The sidebar's places, as GNOME Files has them, found the Windows way (no Qt): Home and its folders (Windows'
known folders, so a Documents moved to OneDrive or another drive is found), drives, bookmarks, starred files and
Recent (Windows' own recent items: the shortcuts in %APPDATA%\\Microsoft\\Windows\\Recent)."""

from __future__ import annotations

import os
import shutil
import string
import struct
import sys
import uuid
from dataclasses import dataclass

WINDOWS = sys.platform == "win32"


@dataclass
class Place:
    label: str
    icon: str                  # an icon name; "" is the divider between groups
    path: str = ""             # a folder, or recent: starred: trash: other:
    eject: bool = False
    kind: str = "place"        # place, bookmark, drive


SPECIAL = {"recent:": "Recent", "starred:": "Starred", "trash:": "Trash", "other:": "Other Locations"}

# Windows' known folders (KNOWNFOLDERID) and Files' names and icons for them.
KNOWN = [("Desktop", "desktop", "B4BFCC3A-DB2C-424C-B029-7FE99A87C641"),
         ("Documents", "documents", "FDD39AD0-238F-46AF-ADB4-6C85480369C7"),
         ("Downloads", "downloads", "374DE290-123F-4565-9164-39C4925E467B"),
         ("Music", "music", "4BD8D571-6D19-48D3-BE97-422220080E43"),
         ("Pictures", "pictures", "33E28130-4E1E-4676-835A-98395C3BC3BB"),
         ("Videos", "videos", "18989B1D-99B5-455B-841C-AB7C74E4DDFC")]


def home() -> str:
    return os.path.expanduser("~")


def known_folder(guid: str) -> str | None:
    """SHGetKnownFolderPath: where Windows says the folder is now."""
    import ctypes
    from ctypes import wintypes

    class GUID(ctypes.Structure):
        _fields_ = [("Data1", wintypes.DWORD), ("Data2", wintypes.WORD), ("Data3", wintypes.WORD),
                    ("Data4", ctypes.c_ubyte * 8)]
    u = uuid.UUID(guid)
    g = GUID(u.time_low, u.time_mid, u.time_hi_version, (ctypes.c_ubyte * 8)(*u.bytes[8:]))
    out = ctypes.c_wchar_p()
    if ctypes.windll.shell32.SHGetKnownFolderPath(ctypes.byref(g), 0, None, ctypes.byref(out)) != 0:
        return None
    try:
        return out.value
    finally:
        ctypes.windll.ole32.CoTaskMemFree(out)


def _xdg_dirs() -> dict[str, str]:
    """Linux: ~/.config/user-dirs.dirs (XDG_MUSIC_DIR="$HOME/Music" ...)."""
    out = {}
    cfg = os.path.join(os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config"), "user-dirs.dirs")
    try:
        with open(cfg, encoding="utf-8") as fh:
            for line in fh:
                if line.startswith("XDG_") and "=" in line:
                    k, v = line.strip().split("=", 1)
                    out[k[4:-4]] = v.strip('"').replace("$HOME", home())
    except OSError:
        pass
    return out


def home_folders() -> list[tuple[str, str, str]]:
    """(label, icon, path) for each of Home's folders that exists."""
    xdg = {} if WINDOWS else _xdg_dirs()
    out = []
    for label, icon, guid in KNOWN:
        path = known_folder(guid) if WINDOWS else xdg.get(label.upper(), os.path.join(home(), label))
        if path and os.path.isdir(path):
            out.append((label, icon, path))
    return out


# --------------------------------------------------------------------------- drives
@dataclass
class Drive:
    root: str          # C:\
    label: str         # "Windows (C:)"
    removable: bool
    free: int = 0
    total: int = 0


def drives() -> list[Drive]:
    out = []
    if WINDOWS:
        import ctypes
        k32 = ctypes.windll.kernel32
        k32.SetErrorMode(1)  # no "insert a disk" boxes for empty card readers
        mask = k32.GetLogicalDrives()
        for i, letter in enumerate(string.ascii_uppercase):
            if not mask & (1 << i):
                continue
            root = f"{letter}:\\"
            kind = k32.GetDriveTypeW(root)  # 2 removable, 3 fixed, 4 network, 5 CD, 6 RAM disk
            if kind in (0, 1):
                continue
            name = ctypes.create_unicode_buffer(261)
            ok = k32.GetVolumeInformationW(root, name, 261, None, None, None, None, 0)
            if not ok and kind in (2, 5):
                continue  # an empty card reader or DVD drive
            default = {2: "USB Drive", 4: "Network Drive", 5: "CD Drive"}.get(kind, "Local Disk")
            d = Drive(root, f"{name.value or default} ({letter}:)", kind in (2, 5))
            try:
                u = shutil.disk_usage(root)
                d.free, d.total = u.free, u.total
            except OSError:
                pass
            out.append(d)
    else:
        u = shutil.disk_usage("/")
        out.append(Drive("/", "Computer", False, u.free, u.total))
        user = os.environ.get("USER", "")
        for base in (f"/media/{user}", f"/run/media/{user}"):
            try:
                for n in sorted(os.listdir(base)):
                    p = os.path.join(base, n)
                    if os.path.ismount(p):
                        u = shutil.disk_usage(p)
                        out.append(Drive(p, n, True, u.free, u.total))
            except OSError:
                pass
    return out


def drive_of(path: str, all_drives: list[Drive] | None = None) -> Drive | None:
    best = None
    for d in all_drives if all_drives is not None else drives():
        r = os.path.normcase(d.root)
        if os.path.normcase(path).startswith(r) and (best is None or len(r) > len(best.root)):
            best = d
    return best


def is_remote(path: str) -> bool:
    """A network location (UNC path or a mapped network drive): Files' "on this computer only" preferences leave
    these out, as reading them is slow."""
    if path.startswith(("\\\\", "//")):
        return True
    if WINDOWS and len(path) >= 2 and path[1] == ":":
        import ctypes
        return ctypes.windll.kernel32.GetDriveTypeW(path[:2] + "\\") == 4  # DRIVE_REMOTE
    return False


# --------------------------------------------------------------------------- Windows network shares
def share_path(address: str) -> str | None:
    """"\\\\server\\share\\folder", "//server/share" or Files' "smb://server/share/folder" -> the UNC path
    ("\\\\server\\share\\folder"), else None."""
    a = address.strip()
    if a.lower().startswith("smb://"):
        a = a[6:]
        if "@" in a.split("/", 1)[0]:
            a = a.split("@", 1)[1]  # smb://user@server/share: the user is asked for when needed
    elif a.startswith(("\\\\", "//")):
        a = a[2:]
    else:
        return None
    parts = [x for x in a.replace("\\", "/").split("/") if x]
    if len(parts) < 2:
        return None  # a share needs a server and a share name
    return "\\\\" + "\\".join(parts)


def share_root(unc: str) -> str:
    """"\\\\server\\share\\a\\b" -> "\\\\server\\share"."""
    parts = unc.lstrip("\\").split("\\")
    return "\\\\" + "\\".join(parts[:2])


def share_label(unc: str) -> str:
    """Files' name for a share: "share on server"."""
    parts = unc.lstrip("\\").split("\\")
    return f"{parts[1]} on {parts[0]}" if len(parts) >= 2 else unc


SHARE_ERRORS = {5: "Access is denied.", 53: "The server can\u2019t be found.", 67: "That share doesn\u2019t exist.",
                86: "The password is wrong.", 1219: "You\u2019re already connected to this server as another user.",
                1326: "The user name or password is wrong.", 1909: "That account is locked."}


def connect_share(unc: str, user: str = "", password: str = "", remember: bool = False) -> None:
    """Log in to a share as Windows' "Map network drive" does, without a drive letter (WNetAddConnection2W).
    `remember` lets Windows keep the password in Credential Manager. Raises OSError with Windows' reason."""
    if not WINDOWS:
        raise OSError(0, "Network shares are a Windows feature.")
    import ctypes
    from ctypes import wintypes

    class NETRESOURCEW(ctypes.Structure):
        _fields_ = [("dwScope", wintypes.DWORD), ("dwType", wintypes.DWORD), ("dwDisplayType", wintypes.DWORD),
                    ("dwUsage", wintypes.DWORD), ("lpLocalName", wintypes.LPWSTR), ("lpRemoteName", wintypes.LPWSTR),
                    ("lpComment", wintypes.LPWSTR), ("lpProvider", wintypes.LPWSTR)]
    nr = NETRESOURCEW(0, 1, 0, 0, None, share_root(unc), None, None)  # RESOURCETYPE_DISK
    flags = 0x1000 if remember else 0                                  # CONNECT_CMD_SAVECRED
    res = ctypes.windll.mpr.WNetAddConnection2W(ctypes.byref(nr), password or None, user or None, flags)
    if res:
        raise OSError(res, SHARE_ERRORS.get(res, ctypes.FormatError(res).strip()), unc)


def disconnect_share(unc: str) -> None:
    if WINDOWS:
        import ctypes
        ctypes.windll.mpr.WNetCancelConnection2W(share_root(unc), 0, False)


def shares() -> list[dict]:
    """The shares you've opened, for the sidebar: [{"path": "\\\\server\\share", "name": "share on server"}]."""
    from . import settings
    return [x for x in settings.get_list("shares") if isinstance(x, dict) and x.get("path")]


def add_share(unc: str) -> None:
    from . import settings
    root = share_root(unc)
    items = [x for x in shares() if x["path"].lower() != root.lower()]
    settings.set("shares", items + [{"path": root, "name": share_label(root)}])


def remove_share(unc: str) -> None:
    from . import settings
    root = share_root(unc)
    settings.set("shares", [x for x in shares() if x["path"].lower() != root.lower()])


def eject(root: str) -> bool:
    """Eject a USB drive or disc as Explorer's Eject does (the shell's Eject verb)."""
    if not WINDOWS:
        return False
    import subprocess
    letter = root[:2]
    script = f"(New-Object -ComObject Shell.Application).Namespace(17).ParseName('{letter}').InvokeVerb('Eject')"
    return subprocess.run(["powershell", "-NoProfile", "-Command", script], creationflags=0x08000000,
                          capture_output=True).returncode == 0


# --------------------------------------------------------------------------- bookmarks and stars (settings)
def bookmarks() -> list[dict]:
    from . import settings
    return [b for b in settings.get_list("bookmarks") if isinstance(b, dict) and b.get("path")]


def set_bookmarks(items: list[dict]) -> None:
    from . import settings
    settings.set("bookmarks", items)


def add_bookmark(path: str, name: str = "") -> bool:
    items = bookmarks()
    if any(os.path.normcase(b["path"]) == os.path.normcase(path) for b in items):
        return False
    items.append({"path": path, "name": name or os.path.basename(os.path.normpath(path)) or path})
    set_bookmarks(items)
    return True


def remove_bookmark(path: str) -> None:
    set_bookmarks([b for b in bookmarks() if os.path.normcase(b["path"]) != os.path.normcase(path)])


def starred() -> list[str]:
    from . import settings
    return [p for p in settings.get_list("starred") if isinstance(p, str)]


def is_starred(path: str, stars: set[str] | None = None) -> bool:
    return os.path.normcase(path) in (stars if stars is not None else {os.path.normcase(p) for p in starred()})


def set_starred(paths: list[str], on: bool) -> None:
    from . import settings
    cur = starred()
    keys = {os.path.normcase(p) for p in paths}
    cur = [p for p in cur if os.path.normcase(p) not in keys]
    if on:
        cur.extend(paths)
    settings.set("starred", cur)


def follow_star(old: str, new: str) -> None:
    """Starred items keep their star when they (or a folder they're in) are renamed or moved."""
    from . import settings
    cur = starred()
    o = os.path.normcase(old)
    changed = [new + p[len(old):] if os.path.normcase(p) == o or os.path.normcase(p).startswith(o + os.sep) else p
               for p in cur]
    if changed != cur:
        settings.set("starred", changed)


# --------------------------------------------------------------------------- Recent
def lnk_target(data: bytes) -> str | None:
    """The target path of a Windows shortcut (.lnk), from its LinkInfo (MS-SHLLINK 2.3): the local base path
    (Unicode if present) plus the common path suffix. None if the shortcut has no LinkInfo."""
    if len(data) < 0x4C or struct.unpack_from("<I", data, 0)[0] != 0x4C:
        return None
    flags = struct.unpack_from("<I", data, 0x14)[0]
    pos = 0x4C
    if flags & 0x1:                               # HasLinkTargetIDList
        pos += 2 + struct.unpack_from("<H", data, pos)[0]
    if not flags & 0x2:                           # HasLinkInfo
        return None
    li = pos
    size, hsize, liflags, _vol, base, _net, suffix = struct.unpack_from("<7I", data, li)
    if not liflags & 0x1:                         # VolumeIDAndLocalBasePath
        return None

    def cstr(off, wide):
        if wide:
            end = off
            while end + 1 < len(data) and data[end:end + 2] != b"\0\0":
                end += 2
            return data[off:end].decode("utf-16-le", "replace")
        end = data.index(b"\0", off)
        return data[off:end].decode("mbcs" if WINDOWS else "cp1252", "replace")
    if hsize >= 0x24:
        base_u, suffix_u = struct.unpack_from("<2I", data, li + 28)
        path = cstr(li + base_u, True) + (cstr(li + suffix_u, True) if suffix_u else "")
    else:
        path = cstr(li + base, False) + cstr(li + suffix, False)
    return path or None


def make_lnk(target: str) -> bytes:
    """A minimal shortcut with a Unicode LinkInfo (tests, and the format reference)."""
    header = struct.pack("<I16sI", 0x4C, uuid.UUID("00021401-0000-0000-C000-000000000046").bytes_le, 0x2 | 0x80)
    header += b"\0" * (0x4C - len(header))
    base_a = b"\0"
    base_u = target.encode("utf-16-le") + b"\0\0"
    hsize = 0x24
    off_base = hsize
    off_suffix = off_base + len(base_a)
    off_base_u = off_suffix + 1
    off_suffix_u = off_base_u + len(base_u)
    body = base_a + b"\0" + base_u + b"\0\0"
    size = hsize + len(body)
    li = struct.pack("<9I", size, hsize, 0x1, 0, off_base, 0, off_suffix, off_base_u, off_suffix_u) + body
    return header + li


def recent_folder() -> str:
    return os.path.join(os.environ.get("APPDATA", ""), "Microsoft", "Windows", "Recent")


def recent(limit: int = 200, folder: str | None = None) -> list[tuple[str, float]]:
    """(path, when used) for recently used files that still exist, newest first. Windows: its Recent shortcuts
    (folders left out, as Files' Recent shows files); Linux: GTK's recently-used.xbel."""
    out: list[tuple[str, float]] = []
    if WINDOWS or folder is not None:
        folder = folder or recent_folder()
        try:
            names = os.listdir(folder)
        except OSError:
            return []
        seen = set()
        for n in names:
            if not n.lower().endswith(".lnk"):
                continue
            p = os.path.join(folder, n)
            try:
                with open(p, "rb") as fh:
                    target = lnk_target(fh.read(65536))
                when = os.path.getmtime(p)
            except (OSError, struct.error, ValueError):
                continue
            if target and os.path.isfile(target) and os.path.normcase(target) not in seen:
                seen.add(os.path.normcase(target))
                out.append((target, when))
    else:
        import urllib.parse
        import xml.etree.ElementTree as ET
        from datetime import datetime
        xbel = os.path.join(os.environ.get("XDG_DATA_HOME") or os.path.expanduser("~/.local/share"),
                            "recently-used.xbel")
        try:
            root = ET.parse(xbel).getroot()
        except (OSError, ET.ParseError):
            return []
        for b in root.iter("bookmark"):
            href = b.get("href", "")
            if not href.startswith("file://"):
                continue
            path = urllib.parse.unquote(href[7:])
            try:
                when = datetime.fromisoformat(b.get("visited") or b.get("modified", "").replace("Z", "+00:00"))
                t = when.timestamp()
            except ValueError:
                t = 0
            if os.path.isfile(path):
                out.append((path, t))
    out.sort(key=lambda x: -x[1])
    return out[:limit]


def add_recent(path: str) -> None:
    """Tell Windows a file was opened (SHAddToRecentDocs), so it shows in Recent here and in Explorer."""
    if WINDOWS:
        import ctypes
        ctypes.windll.shell32.SHAddToRecentDocs(3, ctypes.c_wchar_p(path))  # SHARD_PATHW


def sidebar() -> list[Place]:
    """Files' sidebar order: Recent, Starred, Home, Home's folders, Trash | bookmarks | drives, Other Locations."""
    out = [Place("Recent", "recent", "recent:"), Place("Starred", "star", "starred:"), Place("Home", "home", home())]
    out += [Place(label, icon, path) for label, icon, path in home_folders()]
    out.append(Place("Trash", "trash", "trash:"))
    bms = bookmarks()
    if bms:
        out.append(Place("", ""))
        out += [Place(b.get("name") or b["path"], "bookmark", b["path"], kind="bookmark") for b in bms]
    out.append(Place("", ""))
    out += [Place(d.label, "usb" if d.removable else "drive", d.root, d.removable, "drive") for d in drives()]
    out += [Place(x.get("name") or share_label(x["path"]), "share", x["path"], True, "share") for x in shares()]
    out.append(Place("Other Locations", "network", "other:"))
    return out
