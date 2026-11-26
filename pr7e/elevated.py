"""Copying into folders only an administrator may change (no Qt), the way Explorer does it.

Program Files (C:\\Program Files\\Common Files\\VST3, for one) refuses an ordinary program: Windows answers "Access
is denied" (error 5). Explorer then asks "You'll need to provide administrator permission to copy to this folder",
and on Continue a part of Windows that runs with administrator rights does the copy. PR7-E does the same:

    the copy job sets refused items aside as operations (`op` dicts: what, src, target, replace, merge),
    the window asks once (job.ask_admin), and `run_elevated` starts PR7-E itself with "runas" (Windows' own prompt)
    as `PR7-E.exe --elevated-ops IN.json OUT.json`, which runs `perform` on them and writes back each one's error.

`perform` is plain file work and is what the tests run directly; only the "runas" start needs real Windows.
A file another program has open is a different refusal (error 32, "being used by another process"): administrator
rights don't help there, so it isn't set aside."""

from __future__ import annotations

import errno
import json
import os
import shutil
import stat
import secrets
import subprocess
import sys
import tempfile


def needs_admin(e: BaseException) -> bool:
    """Windows' "Access is denied" (5); elsewhere EACCES (Linux tests)."""
    if not isinstance(e, PermissionError):
        return False
    winerror = getattr(e, "winerror", None)
    if winerror is not None:
        return winerror == 5
    return e.errno in (errno.EACCES, errno.EPERM)


def _remove(path: str):
    def force(func, p, _exc):
        os.chmod(p, stat.S_IWRITE | stat.S_IREAD)
        func(p)
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path, onexc=force)
    elif os.path.lexists(path):
        try:
            os.remove(path)
        except PermissionError:
            force(os.remove, path, None)


def _copy_into(src: str, target: str, merge: bool):
    """A file, or a folder with everything in it; merging into an existing folder replaces same-named files."""
    if os.path.isdir(src) and not os.path.islink(src):
        os.makedirs(target, exist_ok=merge)
        for name in sorted(os.listdir(src)):
            s, t = os.path.join(src, name), os.path.join(target, name)
            if os.path.lexists(t) and not (os.path.isdir(s) and os.path.isdir(t)):
                _remove(t)
            _copy_into(s, t, merge=os.path.isdir(t))
        shutil.copystat(src, target)
    else:
        os.makedirs(os.path.dirname(target) or ".", exist_ok=True)
        shutil.copy2(src, target)


def perform(ops: list[dict]) -> list[str | None]:
    """Do each operation; None for done, else the error's text (in the order given)."""
    out: list[str | None] = []
    for op in ops:
        try:
            src, target = op["src"], op["target"]
            if op.get("replace") and os.path.lexists(target):
                _remove(target)
            if op["what"] == "copy":
                _copy_into(src, target, bool(op.get("merge")))
            elif op["what"] == "trash":                 # to the Recycle Bin (the same user's: same SID)
                from . import trash
                trash.send(src)
            elif op["what"] == "delete":
                _remove(src)
            elif op["what"] == "rename":
                os.rename(src, target)
            elif op["what"] == "mkdir":
                os.mkdir(target)
            elif op["what"] == "move":
                try:
                    os.rename(src, target)
                except OSError:
                    _copy_into(src, target, bool(op.get("merge")))
                    _remove(src)
            else:
                raise ValueError(f"unknown operation {op['what']}")
            out.append(None)
        except (OSError, ValueError, KeyError) as e:
            out.append(getattr(e, "strerror", None) or str(e))
    return out


def own_command() -> tuple[str, list[str]]:
    """How to start PR7-E again: the exe, or Python with the package when run from source."""
    if getattr(sys, "frozen", False):
        return sys.executable, []
    return sys.executable, ["-m", "pr7e"]


def exchange_folder() -> str:
    """The folder both PR7-Es share: a plain folder in Temp, inheriting Temp's access (this user by name).

    Not tempfile.mkdtemp: its mode 0o700 gives, since Python's CVE-2024-4030 fix, a protected list of SYSTEM,
    Administrators and OWNER RIGHTS only, and a file the elevated PR7-E writes is owned by Administrators, so this
    (unelevated) PR7-E couldn't read the results and reported "permission wasn't given" after the work was done."""
    while True:
        folder = os.path.join(tempfile.gettempdir(), f"pr7e-admin-{secrets.token_hex(8)}")
        try:
            os.mkdir(folder)
            return folder
        except FileExistsError:
            continue


def check_done(ops: list[dict]) -> list[str | None]:
    """What the disk shows, when the elevated PR7-E ran but its results can't be read: done = the target is there
    (and, for a move, the original is gone)."""
    out: list[str | None] = []
    for op in ops:
        there = os.path.lexists(op["target"])
        left = op["what"] == "move" and os.path.lexists(op["src"])
        out.append(None if there and not left else "not done (Windows refused it even with administrator rights)")
    return out


def run_elevated(ops: list[dict], timeout: float = 3600) -> list[str | None] | None:
    """Run `perform` in PR7-E started with administrator rights (Windows asks). None when the prompt was refused or
    Windows couldn't start it."""
    if sys.platform != "win32":
        return None
    import ctypes
    from ctypes import wintypes
    folder = exchange_folder()
    src, out = os.path.join(folder, "ops.json"), os.path.join(folder, "done.json")
    with open(src, "w", encoding="utf-8") as f:
        json.dump(ops, f)
    exe, pre = own_command()
    params = subprocess.list2cmdline([*pre, "--elevated-ops", src, out])

    class SHELLEXECUTEINFOW(ctypes.Structure):
        _fields_ = [("cbSize", wintypes.DWORD), ("fMask", ctypes.c_ulong), ("hwnd", wintypes.HWND),
                    ("lpVerb", wintypes.LPCWSTR), ("lpFile", wintypes.LPCWSTR), ("lpParameters", wintypes.LPCWSTR),
                    ("lpDirectory", wintypes.LPCWSTR), ("nShow", ctypes.c_int), ("hInstApp", wintypes.HINSTANCE),
                    ("lpIDList", ctypes.c_void_p), ("lpClass", wintypes.LPCWSTR), ("hkeyClass", wintypes.HKEY),
                    ("dwHotKey", wintypes.DWORD), ("hIconOrMonitor", wintypes.HANDLE), ("hProcess", wintypes.HANDLE)]
    SEE_MASK_NOCLOSEPROCESS, SW_HIDE = 0x40, 0
    info = SHELLEXECUTEINFOW(cbSize=ctypes.sizeof(SHELLEXECUTEINFOW), fMask=SEE_MASK_NOCLOSEPROCESS, lpVerb="runas",
                             lpFile=exe, lpParameters=params, nShow=SW_HIDE)
    try:
        try:
            if not ctypes.windll.shell32.ShellExecuteExW(ctypes.byref(info)) or not info.hProcess:
                return None                     # ERROR_CANCELLED (1223): No on Windows' prompt
            ctypes.windll.kernel32.WaitForSingleObject(info.hProcess, int(timeout * 1000))
            ctypes.windll.kernel32.CloseHandle(info.hProcess)
        except OSError:
            return None
        try:
            with open(out, encoding="utf-8") as f:
                got = json.load(f)
            if isinstance(got, list) and len(got) == len(ops):
                return got
        except (OSError, ValueError):
            pass
        return check_done(ops)                  # it ran (Windows said yes): the disk says what it did
    finally:
        shutil.rmtree(folder, ignore_errors=True)


def main(src: str, out: str) -> int:
    """`PR7-E.exe --elevated-ops IN OUT` (started by run_elevated, with administrator rights)."""
    try:
        with open(src, encoding="utf-8") as f:
            ops = json.load(f)
        result = perform(ops)
    except (OSError, ValueError) as e:
        result = [str(e)]
    with open(out, "w", encoding="utf-8") as f:
        json.dump(result, f)
    return 0
