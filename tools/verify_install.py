"""Check a real install of PR7-E-Setup.exe (CI, Windows): python tools/verify_install.py installed|uninstalled

`installed` runs the installed PR7-E.exe offscreen on a folder (--selftest) and checks what it listed, so the
frozen program is shown to start with Qt, its plugins and the Inter fonts; it also checks "Open in PR7-E" in
Explorer's folder menu and, as CI installs with /MERGETASKS=defaultfolders, "Use PR7-E for Folders": folders' and
drives' default action and Win+E. `uninstalled` checks all of it is gone."""
import json
import os
import subprocess
import sys
import tempfile
import time
import winreg
from pathlib import Path

HKCU = winreg.HKEY_CURRENT_USER
APP = Path(os.environ["LOCALAPPDATA"]) / "Programs" / "PR7-E"
SHORTCUT = Path(os.environ["APPDATA"]) / "Microsoft" / "Windows" / "Start Menu" / "Programs" / "Paragon PR7-E.lnk"
UNINSTALL = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\{6B8E2F14-3C5A-4D97-A1E0-7F29C4B85D36}_is1"
MENU = [r"Software\Classes\Directory\shell\PR7E", r"Software\Classes\Drive\shell\PR7E",
        r"Software\Classes\Directory\Background\shell\PR7E"]
DEFAULTS = [r"Software\Classes\Directory\shell", r"Software\Classes\Drive\shell"]
WIN_E = r"Software\Classes\CLSID\{52205fd8-5dfb-447d-801a-d0b52f2e83e1}"


def value(path, name=""):
    try:
        k = winreg.OpenKey(HKCU, path)
    except OSError:
        return None
    try:
        return winreg.QueryValueEx(k, name)[0]
    except OSError:
        return None
    finally:
        winreg.CloseKey(k)


def check(ok, what, p):
    print(("ok    " if ok else "FAIL  ") + what)
    if not ok:
        p.append(what)


def installed():
    p = []
    exe = APP / "PR7-E.exe"
    for f in (exe, APP / "unins000.exe", APP / "_internal" / "pr7e" / "fonts" / "Inter-Regular.ttf",
              APP / "_internal" / "pr7e" / "icons" / "app.ico"):
        check(f.is_file(), f"file {f}", p)
    check(SHORTCUT.is_file(), "Start menu shortcut", p)
    check(value(UNINSTALL, "DisplayName") == "Paragon PR7-E", "Installed apps entry", p)
    for key in MENU:
        check(value(key) == "Open in PR7-E", f"Explorer menu {key}", p)
        cmd = value(key + r"\command") or ""
        check(str(exe).lower() in cmd.lower() and "--explorer" in cmd, f"  its command: {cmd}", p)
    for key in DEFAULTS:
        check(value(key) == "PR7E", f"Use PR7-E for Folders: {key} default", p)
    cmd = value(WIN_E + r"\shell\opennewwindow\command") or ""
    check(str(exe).lower() in cmd.lower() and value(WIN_E + r"\shell\opennewwindow\command", "DelegateExecute") == "",
          f"Use PR7-E for Folders: Win+E runs {cmd}", p)
    with tempfile.TemporaryDirectory() as d:
        folder = Path(d) / "folder"
        (folder / "Pictures").mkdir(parents=True)
        (folder / "notes.txt").write_text("notes")
        out = Path(d) / "out.json"
        subprocess.run([str(exe), "--selftest", str(folder), str(out)], timeout=120, check=False)
        got = json.loads(out.read_text(encoding="utf-8")) if out.exists() else {}
        check(got.get("names") == ["Pictures", "notes.txt"] and got.get("fonts") is True,
              f"PR7-E.exe opens a folder ({got})", p)
    # Connect to real servers with the installed program: TLS, paramiko and cryptography made it into the build.
    sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tests"))
    from servers import PASSWORD, USER, FTPServer, SFTPServerThread
    with tempfile.TemporaryDirectory() as d:
        root = Path(d) / "server"
        (root / "music").mkdir(parents=True)
        (root / "index.html").write_bytes(b"hi")
        for name, start, scheme in (("FTP over TLS", lambda: FTPServer(str(root), True, d), "ftpes"),
                                    ("SFTP", lambda: SFTPServerThread(str(root)), "sftp")):
            srv = start()
            try:
                out = Path(d) / f"{scheme}.json"
                subprocess.run([str(exe), "--check", f"{scheme}://{USER}:{PASSWORD}@127.0.0.1:{srv.port}", str(out)],
                               timeout=120, check=False)
                got = json.loads(out.read_text(encoding="utf-8")) if out.exists() else {}
                check(got.get("names") == ["index.html", "music"] and got.get("encrypted") is True,
                      f"PR7-E.exe connects over {name} ({got})", p)
            finally:
                srv.stop()
    return p


def uninstalled():
    deadline = time.time() + 60
    while True:
        p = []
        check(not (APP / "PR7-E.exe").exists(), "program files removed", p)
        check(not SHORTCUT.exists(), "Start menu shortcut removed", p)
        check(value(UNINSTALL, "DisplayName") is None, "Installed apps entry removed", p)
        for key in MENU:
            check(value(key) is None, f"Explorer menu {key} removed", p)
        for key in DEFAULTS:
            check(value(key) is None, f"{key} default back to Explorer's", p)
        check(value(WIN_E + r"\shell\opennewwindow\command") is None, "Win+E back to Explorer", p)
        if not p or time.time() > deadline:
            return p
        print("... waiting for the uninstaller to finish")
        time.sleep(3)


if __name__ == "__main__":
    problems = {"installed": installed, "uninstalled": uninstalled}[sys.argv[1]]()
    print("\nALL OK" if not problems else f"\n{len(problems)} problem(s)")
    sys.exit(1 if problems else 0)
