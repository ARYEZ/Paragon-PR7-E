"""Start PR7-E.

    PR7-E.exe [folder ...]               a window, a tab per folder (Home if none)
    PR7-E.exe --explorer FOLDER          from Explorer's "Open in PR7-E"
    PR7-E.exe --register-menu            add "Open in PR7-E" to Explorer's folder menu (an installer option)
    PR7-E.exe --unregister-menu          take that item away
    PR7-E.exe --default-folders on|off   Use PR7-E for Folders: double-clicked folders and Win+E open PR7-E
    PR7-E.exe --unregister               take everything away again (the uninstaller)
    PR7-E.exe --version
    PR7-E.exe --check URL OUT.json       connect to a server (FTP / FTPS / SFTP), list its top folder, write what was
                                         found (CI checks the installed program with it)
    PR7-E.exe --selftest FOLDER OUT.json  open a window offscreen on FOLDER, write what it lists (CI checks the
                                         installed program with it; settings go to a temp folder)
"""

from __future__ import annotations

import os
import sys


def folders_from(argv: list[str]) -> list[str]:
    """The folders on the command line. Explorer passes a drive as "C:\\" in quotes, and Windows' command-line rules
    read that backslash as escaping the quote: the program gets C:" . Stripped, "C:" would mean the current folder
    on drive C, so a bare drive letter is taken as the drive's root."""
    import re
    out = []
    for a in argv:
        if a.startswith("--"):
            continue
        a = a.strip().strip('"').strip()
        if re.fullmatch(r"[A-Za-z]:\\?", a):
            a = a[0].upper() + ":" + os.sep
        if os.path.isdir(a):
            out.append(os.path.abspath(a))
        elif os.path.isfile(a):
            out.append(os.path.dirname(os.path.abspath(a)))
    return out


def check(url: str, out: str) -> int:
    import json
    from . import servers
    result = {}
    try:
        site, path = servers.parse(url)
        srv = servers.connect(site, hostkey=lambda *a: "once", cert=lambda *a: True)
        result = {"encrypted": srv.session.encrypted, "names": sorted(e.name for e in srv.list("/"))}
        servers.disconnect(srv.key)
    except Exception as e:  # noqa: BLE001 - reported in the output
        result = {"error": f"{type(e).__name__}: {e}"}
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(result, fh)
    return 0 if "names" in result else 1


def selftest(folder: str, out: str) -> int:
    import json
    import tempfile
    os.environ["QT_QPA_PLATFORM"] = "offscreen"
    os.environ["PR7E_TRASH"] = tempfile.mkdtemp()
    from PySide6.QtCore import QSettings
    from PySide6.QtWidgets import QApplication
    QSettings.setDefaultFormat(QSettings.IniFormat)
    QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, tempfile.mkdtemp())
    result = {}
    try:
        from . import __version__, theme
        app = QApplication.instance() or QApplication(sys.argv[:1])
        theme.load_fonts()
        app.setStyleSheet(theme.stylesheet())
        from .main_window import MainWindow
        w = MainWindow([folder])
        w.show()
        app.processEvents()
        result = {"version": __version__, "names": [e.name for e in w.tab.model.rows],
                  "fonts": "Inter" in __import__("PySide6.QtGui", fromlist=["QFontDatabase"]).QFontDatabase.families()}
        w.close()
    except Exception as e:  # noqa: BLE001 - reported in the output
        result = {"error": f"{type(e).__name__}: {e}"}
    with open(out, "w", encoding="utf-8") as fh:
        json.dump(result, fh)
    return 0 if "names" in result else 1


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv[:1] == ["--version"]:
        from . import __version__
        print(__version__)
        return 0
    if argv[:1] == ["--check"] and len(argv) == 3:
        return check(argv[1], argv[2])
    if argv[:1] == ["--selftest"] and len(argv) == 3:
        return selftest(argv[1], argv[2])
    if argv[:1] and argv[0] in ("--register-menu", "--register", "--unregister-menu", "--unregister",
                                "--default-folders"):
        from . import shell
        return shell.run(argv)
    from PySide6.QtGui import QIcon
    from PySide6.QtWidgets import QApplication

    from . import settings, theme
    app = QApplication.instance() or QApplication(sys.argv[:1])
    app.setOrganizationName("Paragon")
    app.setApplicationName("PR7-E")
    theme.set_finish(settings.get("finish", "obsidian"))
    theme.load_fonts()
    app.setPalette(theme.palette())
    app.setStyleSheet(theme.stylesheet())
    icon = os.path.join(os.path.dirname(__file__), "icons", "app.ico")
    if os.path.exists(icon):
        app.setWindowIcon(QIcon(icon))
    from .main_window import MainWindow
    w = MainWindow(folders_from(argv) or None)
    w.show()
    from . import update_ui
    update_ui.start(w)  # a check for updates, once a day
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
