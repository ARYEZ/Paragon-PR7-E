"""Start PR7-E.

    PR7-E.exe [folder ...]               a window, a tab per folder (Home if none)
    PR7-E.exe --explorer FOLDER          from Explorer's "Open in PR7-E"
    PR7-E.exe --register-menu            add "Open in PR7-E" to Explorer's folder menu (the installer does this)
    PR7-E.exe --unregister               take it away again (the uninstaller)
    PR7-E.exe --version
    PR7-E.exe --selftest FOLDER OUT.json  open a window offscreen on FOLDER, write what it lists (CI checks the
                                         installed program with it; settings go to a temp folder)
"""

from __future__ import annotations

import os
import sys


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
    if argv[:1] == ["--selftest"] and len(argv) == 3:
        return selftest(argv[1], argv[2])
    if argv[:1] in (["--register-menu"], ["--register"], ["--unregister"]):
        from . import shell
        return shell.run(argv[0])
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
    folders = []
    for a in argv:
        if a == "--explorer":
            continue
        a = a.strip('"')
        if os.path.isdir(a):
            folders.append(os.path.abspath(a))
        elif os.path.isfile(a):
            folders.append(os.path.dirname(os.path.abspath(a)))
    w = MainWindow(folders or None)
    w.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
