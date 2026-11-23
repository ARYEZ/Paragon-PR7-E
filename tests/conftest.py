import os
import sys
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
# Tests never touch the real Recycle Bin: PR7E_TRASH points Trash at a folder of its own (per test, below).
os.environ["PR7E_TRASH"] = tempfile.mkdtemp(prefix="pr7e-trash-")


@pytest.fixture(scope="session")
def qapp(tmp_path_factory):
    # Tests never write the real settings: QSettings goes to INI files in a temp folder.
    from PySide6.QtCore import QSettings
    from PySide6.QtWidgets import QApplication
    from pr7e import theme
    QSettings.setDefaultFormat(QSettings.IniFormat)
    QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, str(tmp_path_factory.mktemp("settings")))
    app = QApplication.instance() or QApplication([])
    theme.load_fonts()
    app.setStyleSheet(theme.stylesheet())
    yield app


@pytest.fixture(autouse=True)
def own_trash(tmp_path, monkeypatch):
    monkeypatch.setenv("PR7E_TRASH", str(tmp_path / "_trash"))
    yield


@pytest.fixture
def clean_settings(qapp):
    from PySide6.QtCore import QSettings
    s = QSettings(QSettings.defaultFormat(), QSettings.UserScope, "Paragon", "PR7-E")
    s.clear()
    s.sync()
    yield
    s.clear()
    s.sync()
