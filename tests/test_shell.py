"""Explorer integration: what each switch writes, and that switching off puts back exactly what was there."""
import os
import sys

import pytest

from pr7e import shell

C = shell.CLASSES + "\\"


class FakeRegistry:
    """HKCU as a dict (case-insensitive keys, like the registry)."""

    def __init__(self):
        self.keys: dict[str, dict] = {}

    def get(self, path, name=""):
        return self.keys.get(path.lower(), {}).get(name)

    def set(self, path, name, value):
        p = path.lower()
        parts = p.split("\\")
        for i in range(1, len(parts) + 1):
            self.keys.setdefault("\\".join(parts[:i]), {})
        self.keys[p][name] = value

    def delete_value(self, path, name=""):
        self.keys.get(path.lower(), {}).pop(name, None)

    def delete_tree(self, path):
        p = path.lower()
        for k in [k for k in self.keys if k == p or k.startswith(p + "\\")]:
            del self.keys[k]

    def exists(self, path):
        return path.lower() in self.keys


@pytest.fixture(params=["fake", "windows"])
def reg(request):
    if request.param == "fake":
        yield FakeRegistry()
        return
    if sys.platform != "win32" or not os.environ.get("CI"):
        pytest.skip("the real registry: Windows CI only")
    r = shell.Registry(prefix="Software\\PR7E-Test\\")  # a throwaway key, never the real Classes
    r.delete_tree("")
    yield r
    import winreg
    shell.Registry().delete_tree("Software\\PR7E-Test")
    assert winreg  # imported for the cleanup's sake


def test_menu_item(reg):
    shell.register_menu(reg)
    assert reg.get(C + r"Directory\shell\PR7E") == "Open in PR7-E"
    assert reg.get(C + r"Drive\shell\PR7E\command").endswith('--explorer "%1"')
    assert reg.get(C + r"Directory\Background\shell\PR7E\command").endswith('--explorer "%V"')
    assert shell.menu_registered(reg) and not shell.folders_on(reg)
    shell.unregister_menu(reg)
    assert not shell.menu_registered(reg)


def test_use_for_folders_and_back(reg):
    shell.set_folders(True, reg)
    assert shell.folders_on(reg)
    assert reg.get(C + r"Directory\shell") == "PR7E" and reg.get(C + r"Drive\shell") == "PR7E"
    win_e = C + shell.WIN_E_COMMAND
    assert reg.get(win_e) == shell._exe() and reg.get(win_e, "DelegateExecute") == ""
    assert reg.get(C + r"Directory\shell\PR7E\command")      # the verb it runs is there
    shell.set_folders(False, reg)
    assert not shell.folders_on(reg)
    assert reg.get(C + r"Directory\shell") is None and reg.get(C + r"Drive\shell") is None
    assert not reg.exists(C + shell.WIN_E)
    assert not shell.menu_registered(reg)                   # the menu item wasn't asked for: gone too


def test_switches_are_independent(reg):
    shell.register_menu(reg)
    shell.set_folders(True, reg)
    shell.set_folders(False, reg)
    assert shell.menu_registered(reg)                       # turning folders off keeps the asked-for menu item
    shell.set_folders(True, reg)
    shell.unregister_menu(reg)
    assert shell.folders_on(reg) and reg.get(C + r"Directory\shell\PR7E\command")  # folders still work


def test_another_file_managers_default_comes_back(reg):
    reg.set(C + r"Directory\shell", "", "openindopus")
    reg.set(C + shell.WIN_E_COMMAND, "", r"C:\Other\other.exe")
    shell.set_folders(True, reg)
    assert reg.get(C + r"Directory\shell") == "PR7E"
    shell.set_folders(False, reg)
    assert reg.get(C + r"Directory\shell") == "openindopus"
    assert reg.get(C + r"Drive\shell") is None
    assert reg.get(C + shell.WIN_E_COMMAND) == r"C:\Other\other.exe"


def test_uninstall_takes_everything_away(reg):
    shell.register_menu(reg)
    shell.set_folders(True, reg)
    shell.unregister(reg)
    assert not shell.menu_registered(reg) and not shell.folders_on(reg)
    assert not reg.exists(C + shell.WIN_E) and not reg.exists(shell.STATE)
    assert reg.get(C + r"Directory\shell") is None


def test_drive_letters_from_explorer(tmp_path):
    from pr7e.app import folders_from
    assert folders_from(["--explorer", str(tmp_path)]) == [str(tmp_path)]
    f = tmp_path / "a.txt"
    f.write_text("a")
    assert folders_from([str(f)]) == [str(tmp_path)]
    if sys.platform == "win32":
        drive = os.path.splitdrive(str(tmp_path))[0]       # "C:"
        # Explorer's "C:\" arrives as C:" (the backslash escapes the quote): the root, not C's current folder.
        assert folders_from(["--explorer", drive + '"']) == [drive + "\\"]
        assert folders_from([drive]) == [drive + "\\"]


def test_window_switch(qapp, clean_settings, tmp_path, monkeypatch):
    from pr7e import places
    from pr7e.main_window import MainWindow
    monkeypatch.setattr(places, "home", lambda: str(tmp_path))
    w = MainWindow([str(tmp_path)])
    w.shell_reg = FakeRegistry()
    asked = []
    w.answers = {"confirm": lambda p: (asked.append(p["heading"]), False)[1], "menu": lambda m: None}
    w.set_default_folders(True)
    assert asked == ["Open folders in PR7-E instead of File Explorer?"] and not shell.folders_on(w.shell_reg)
    assert not w.act["folders"].isChecked()
    w.answers["confirm"] = lambda p: True
    w.set_default_folders(True)
    assert shell.folders_on(w.shell_reg) and w.act["folders"].isChecked()
    w._main_menu()
    assert w.act["folders"].isEnabled() and w.act["folders"].isChecked()
    w.set_default_folders(False)
    assert not shell.folders_on(w.shell_reg) and not w.act["folders"].isChecked()
    w.close()
