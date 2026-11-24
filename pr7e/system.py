"""Talking to Windows (no Qt): opening files with their app, Windows' "Open with" chooser, the default app's and
the file type's names (for the menu and Properties), and a terminal in a folder. On Linux, the freedesktop
equivalents, so the tests and Ubuntu work too."""

from __future__ import annotations

import os
import shutil
import subprocess
import sys

from . import places

WINDOWS = sys.platform == "win32"
_NO_WINDOW = 0x08000000


def open_path(path: str) -> None:
    """Open a file with its app (Explorer's double-click); it's added to Recent."""
    if WINDOWS:
        os.startfile(path)  # noqa: S606 - opening the user's own file is the point
        places.add_recent(path)
    else:
        subprocess.Popen(["xdg-open", path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def open_with(path: str) -> None:
    """Windows' own "How do you want to open this file?" chooser."""
    if WINDOWS:
        subprocess.Popen(["rundll32.exe", "shell32.dll,OpenAs_RunDLL", path])
    else:
        open_path(path)


def _assoc(ext: str, what: int) -> str | None:
    if not WINDOWS or not ext:
        return None
    import ctypes
    from ctypes import wintypes
    size = wintypes.DWORD(512)
    buf = ctypes.create_unicode_buffer(512)
    res = ctypes.windll.shlwapi.AssocQueryStringW(0x20, what, ext, None, buf, ctypes.byref(size))  # INIT_IGNOREUNKNOWN
    return (buf.value or None) if res == 0 else None


def default_app(path: str) -> str | None:
    """The name of the app the file opens with ("Photos", "Paragon PR7-L"), or None."""
    return _assoc(os.path.splitext(path)[1], 4)  # ASSOCSTR_FRIENDLYAPPNAME


def type_name(path: str, is_dir: bool) -> str:
    """What Properties calls the file: Windows' name for its type ("JPEG image"), else "MP3 file"."""
    if is_dir:
        return "Folder"
    ext = os.path.splitext(path)[1]
    name = _assoc(ext, 3)  # ASSOCSTR_FRIENDLYDOCNAME
    if name:
        return name
    return f"{ext[1:].upper()} file" if ext else "File"


def terminal(folder: str) -> None:
    """Open a terminal in the folder (Files' "Open in Terminal"): Windows Terminal if it's there, else cmd."""
    if WINDOWS:
        wt = shutil.which("wt.exe")
        if wt:
            subprocess.Popen([wt, "-d", folder])
        else:
            subprocess.Popen(["cmd.exe", "/K"], cwd=folder, creationflags=subprocess.CREATE_NEW_CONSOLE)
    else:
        for term in ("x-terminal-emulator", "gnome-terminal", "konsole", "xterm"):
            if shutil.which(term):
                subprocess.Popen([term], cwd=folder)
                return


# --------------------------------------------------------------------------- Properties' Permissions
def owner(path: str) -> str:
    """Who owns the file ("PC\\me"), from its security descriptor on Windows; the user name elsewhere."""
    if WINDOWS:
        import ctypes
        from ctypes import wintypes
        adv = ctypes.windll.advapi32
        sid, sd = ctypes.c_void_p(), ctypes.c_void_p()
        adv.GetNamedSecurityInfoW.argtypes = [wintypes.LPCWSTR, ctypes.c_int, wintypes.DWORD,
                                              ctypes.POINTER(ctypes.c_void_p), ctypes.c_void_p, ctypes.c_void_p,
                                              ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
        if adv.GetNamedSecurityInfoW(path, 1, 1, ctypes.byref(sid), None, None, None, ctypes.byref(sd)):
            return ""  # SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION
        try:
            name, dom = ctypes.create_unicode_buffer(256), ctypes.create_unicode_buffer(256)
            n, d, use = wintypes.DWORD(256), wintypes.DWORD(256), wintypes.DWORD()
            adv.LookupAccountSidW.argtypes = [wintypes.LPCWSTR, ctypes.c_void_p, wintypes.LPWSTR,
                                              ctypes.POINTER(wintypes.DWORD), wintypes.LPWSTR,
                                              ctypes.POINTER(wintypes.DWORD), ctypes.POINTER(wintypes.DWORD)]
            if not adv.LookupAccountSidW(None, sid, name, ctypes.byref(n), dom, ctypes.byref(d), ctypes.byref(use)):
                return ""
            return f"{dom.value}\\{name.value}" if dom.value else name.value
        finally:
            ctypes.windll.kernel32.LocalFree(sd)
    try:
        import pwd
        return pwd.getpwuid(os.stat(path).st_uid).pw_name
    except (ImportError, KeyError, OSError):
        return ""


def is_readonly(path: str) -> bool:
    import stat
    st = os.stat(path)
    if WINDOWS:
        return bool(st.st_file_attributes & stat.FILE_ATTRIBUTE_READONLY)
    return not st.st_mode & stat.S_IWUSR


def set_readonly(path: str, on: bool) -> None:
    """Explorer's Read-only box: on Windows os.chmod sets or clears the read-only attribute."""
    import stat
    mode = os.stat(path).st_mode
    os.chmod(path, (mode & ~(stat.S_IWUSR | stat.S_IWGRP | stat.S_IWOTH)) if on else (mode | stat.S_IWUSR))


def is_hidden_attr(path: str) -> bool:
    if not WINDOWS:
        return os.path.basename(path).startswith(".")
    import stat
    return bool(os.stat(path).st_file_attributes & stat.FILE_ATTRIBUTE_HIDDEN)


def set_hidden(path: str, on: bool) -> None:
    """Explorer's Hidden box (Windows' hidden attribute)."""
    if not WINDOWS:
        raise OSError(0, "On this system, names starting with a dot are hidden.")
    import ctypes
    import stat
    attrs = os.stat(path).st_file_attributes
    attrs = attrs | stat.FILE_ATTRIBUTE_HIDDEN if on else attrs & ~stat.FILE_ATTRIBUTE_HIDDEN
    if not ctypes.windll.kernel32.SetFileAttributesW(path, attrs & 0xFFFF or 0x80):  # NORMAL when nothing else
        raise ctypes.WinError()
