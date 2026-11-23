"""Windows Explorer integration, all per-user (HKEY_CURRENT_USER, no admin rights):

- "Open in PR7-E" on the right-click menu of folders, drives and a folder's empty space (registry verbs);
- "Use PR7-E for Folders": that verb becomes the folder's and the drive's default action, so double-clicking a
  folder anywhere (desktop, shortcuts, other programs) opens PR7-E, and Win+E opens it too (Windows' per-user
  override of the Win+E command). The way Directory Opus and similar programs take over folders.

Windows' special places (This PC, Control Panel, the Recycle Bin's own window) belong to the shell's "Folder"
class, which is left alone on purpose: taking it over is what breaks those places in other replacements. The taskbar,
Start and the desktop are explorer.exe itself and stay.

What each switch wants is kept in HKCU\\Software\\Paragon\\PR7-E\\Shell (Menu, Folders), so turning one off never
takes away the other; a default action that was there before (another file manager's) is kept there and put back.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

LABEL = "Open in PR7-E"
CLASSES = r"Software\Classes"
STATE = r"Software\Paragon\PR7-E\Shell"
VERB = "PR7E"
VERBS = {r"Directory\shell\PR7E": "%1", r"Drive\shell\PR7E": "%1", r"Directory\Background\shell\PR7E": "%V"}
DEFAULTS = [r"Directory\shell", r"Drive\shell"]          # whose default action "Use PR7-E for Folders" sets
WIN_E = r"CLSID\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}"    # what Win+E opens ("This PC" / Home)
WIN_E_COMMAND = WIN_E + r"\shell\opennewwindow\command"
# Kept for verify_install and older callers: the menu keys under HKCU.
KEYS = {CLASSES + "\\" + k: v for k, v in VERBS.items()}


def available() -> bool:
    return os.name == "nt"


# --------------------------------------------------------------------------- the registry
class Registry:
    """HKEY_CURRENT_USER, with every path under `prefix` (tests on Windows use a throwaway key)."""

    def __init__(self, prefix: str = ""):
        self.prefix = prefix

    def _p(self, path):
        return self.prefix + path

    def get(self, path: str, name: str = "") -> str | None:
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self._p(path)) as k:
                return winreg.QueryValueEx(k, name)[0]
        except OSError:
            return None

    def set(self, path: str, name: str, value) -> None:
        import winreg
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, self._p(path), 0, winreg.KEY_WRITE) as k:
            if isinstance(value, int):
                winreg.SetValueEx(k, name, 0, winreg.REG_DWORD, value)
            else:
                winreg.SetValueEx(k, name, 0, winreg.REG_SZ, value)

    def delete_value(self, path: str, name: str = "") -> None:
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, self._p(path), 0, winreg.KEY_SET_VALUE) as k:
                winreg.DeleteValue(k, name)
        except OSError:
            pass

    def delete_tree(self, path: str) -> None:
        import winreg
        full = self._p(path)
        try:
            k = winreg.OpenKey(winreg.HKEY_CURRENT_USER, full, 0, winreg.KEY_ALL_ACCESS)
        except OSError:
            return
        subs = []
        i = 0
        while True:
            try:
                subs.append(winreg.EnumKey(k, i))
                i += 1
            except OSError:
                break
        winreg.CloseKey(k)
        for sub in subs:
            self.delete_tree(f"{path}\\{sub}")
        try:
            winreg.DeleteKey(winreg.HKEY_CURRENT_USER, full)
        except OSError:
            pass

    def exists(self, path: str) -> bool:
        import winreg
        try:
            winreg.CloseKey(winreg.OpenKey(winreg.HKEY_CURRENT_USER, self._p(path)))
            return True
        except OSError:
            return False


def _reg(reg):
    return reg if reg is not None else Registry()


# --------------------------------------------------------------------------- what the keys hold
def launcher() -> list[str]:
    if getattr(sys, "frozen", False):
        return [sys.executable]
    py = Path(sys.executable)
    pyw = py.with_name("pythonw.exe")
    return [str(pyw if pyw.exists() else py), "-m", "pr7e"]


def _exe() -> str:
    return " ".join(f'"{p}"' if (" " in p or p.lower().endswith(".exe")) else p for p in launcher())


def command(arg: str = "%1") -> str:
    return f'{_exe()} --explorer "{arg}"'


def icon() -> str:
    return f"{sys.executable},0" if getattr(sys, "frozen", False) else str(Path(__file__).parent / "icons" / "app.ico")


def _write_verbs(reg) -> None:
    for key, arg in VERBS.items():
        path = CLASSES + "\\" + key
        reg.set(path, "", LABEL)
        reg.set(path, "MUIVerb", LABEL)
        reg.set(path, "Icon", icon())
        reg.set(path + r"\command", "", command(arg))


def _remove_verbs(reg) -> None:
    for key in VERBS:
        reg.delete_tree(CLASSES + "\\" + key)


def _wanted(reg, name: str) -> bool:
    v = reg.get(STATE, name)
    return str(v) == "1"


# --------------------------------------------------------------------------- the right-click item
def register_menu(reg=None) -> None:
    reg = _reg(reg)
    reg.set(STATE, "Menu", 1)
    _write_verbs(reg)
    _notify()


def unregister_menu(reg=None) -> None:
    """Take the right-click item away; it stays if "Use PR7-E for Folders" is on (it's what that switch runs)."""
    reg = _reg(reg)
    reg.set(STATE, "Menu", 0)
    if not folders_on(reg):
        _remove_verbs(reg)
    _notify()


def menu_registered(reg=None) -> bool:
    if reg is None and not available():
        return False
    return _reg(reg).exists(CLASSES + "\\" + next(iter(VERBS)))


# --------------------------------------------------------------------------- Use PR7-E for Folders
def folders_on(reg=None) -> bool:
    if reg is None and not available():
        return False
    reg = _reg(reg)
    return all(reg.get(CLASSES + "\\" + k) == VERB for k in DEFAULTS)


def set_folders(on: bool, reg=None) -> None:
    reg = _reg(reg)
    if on:
        _write_verbs(reg)
        for k in DEFAULTS:
            path = CLASSES + "\\" + k
            before = reg.get(path)
            if before and before != VERB:  # another program's default: kept, to put back later
                reg.set(STATE, "Previous " + k, before)
            reg.set(path, "", VERB)
        before = reg.get(CLASSES + "\\" + WIN_E_COMMAND)
        if before and reg.get(CLASSES + "\\" + WIN_E, "PR7E-Owned") is None:
            reg.set(STATE, "Previous Win+E", before)
        reg.set(CLASSES + "\\" + WIN_E_COMMAND, "", _exe())
        reg.set(CLASSES + "\\" + WIN_E_COMMAND, "DelegateExecute", "")  # empty: run the command, not Explorer
        reg.set(CLASSES + "\\" + WIN_E, "PR7E-Owned", 1)
        reg.set(STATE, "Folders", 1)
    else:
        for k in DEFAULTS:
            path = CLASSES + "\\" + k
            prev = reg.get(STATE, "Previous " + k)
            if prev:
                reg.set(path, "", prev)
                reg.delete_value(STATE, "Previous " + k)
            elif reg.get(path) == VERB:
                reg.delete_value(path)
        if reg.get(CLASSES + "\\" + WIN_E, "PR7E-Owned") is not None:
            prev = reg.get(STATE, "Previous Win+E")
            if prev:
                reg.set(CLASSES + "\\" + WIN_E_COMMAND, "", prev)
                reg.delete_value(CLASSES + "\\" + WIN_E, "PR7E-Owned")
                reg.delete_value(STATE, "Previous Win+E")
            else:
                reg.delete_tree(CLASSES + "\\" + WIN_E)
        reg.set(STATE, "Folders", 0)
        if not _wanted(reg, "Menu"):
            _remove_verbs(reg)
    _notify()


def unregister(reg=None) -> None:
    """The uninstaller: everything PR7-E put in the registry goes, and folders open in Explorer again."""
    reg = _reg(reg)
    if folders_on(reg) or reg.get(CLASSES + "\\" + WIN_E, "PR7E-Owned") is not None:
        set_folders(False, reg)
    _remove_verbs(reg)
    reg.delete_tree(STATE)
    _notify()


def _notify():
    try:
        import ctypes
        ctypes.windll.shell32.SHChangeNotify(0x08000000, 0, None, None)  # SHCNE_ASSOCCHANGED
    except Exception:  # noqa: BLE001
        pass


def run(argv: list[str]) -> int:
    """The installer's and the command line's switches:
    --register-menu, --unregister-menu, --default-folders on|off, --unregister (everything)."""
    if not available():
        print("Explorer integration is for Windows.")
        return 1
    action = argv[0]
    try:
        if action in ("--register", "--register-menu"):
            register_menu()
        elif action == "--unregister-menu":
            unregister_menu()
        elif action == "--default-folders":
            set_folders((argv[1:] or ["on"])[0].lower() in ("on", "1", "yes", "true"))
        else:
            unregister()
    except OSError as e:
        print(f"{action} failed: {e}")
        return 2
    return 0
