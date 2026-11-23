import os
import sys
import tempfile
from pathlib import Path

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
# Tests never touch the real Recycle Bin: PR7E_TRASH points Trash at a folder of its own (per test, below).
os.environ["PR7E_TRASH"] = tempfile.mkdtemp(prefix="pr7e-trash-")
# ...nor the real data folder (known hosts) or Windows Credential Manager.
os.environ["PR7E_DATA"] = tempfile.mkdtemp(prefix="pr7e-data-")
os.environ["PR7E_MEMORY_KEYRING"] = "1"
sys.path.insert(0, str(Path(__file__).resolve().parent))


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
    # Qt's offscreen platform crashes at exit if text is left on its clipboard (copying server items puts their
    # addresses there): clear it while Qt is still up.
    app.clipboard().clear()
    app.processEvents()


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


@pytest.fixture
def server_root(tmp_path):
    root = tmp_path / "server"
    (root / "music" / "elseworld").mkdir(parents=True)
    (root / "music" / "elseworld" / "01 Portal.flac").write_bytes(os.urandom(300_000))
    (root / "music" / "notes.txt").write_bytes(b"server notes")
    (root / "index.html").write_bytes(b"<html>hi</html>")
    return root


@pytest.fixture(params=["sftp", "ftp", "ftpes"])
def server(request, server_root, tmp_path):
    """A real server on 127.0.0.1 and its address."""
    from servers import PASSWORD, USER, FTPServer, SFTPServerThread
    if request.param == "sftp":
        s = SFTPServerThread(str(server_root))
    else:
        s = FTPServer(str(server_root), tls=request.param == "ftpes", certdir=str(tmp_path))
    s.address = f"{request.param}://{USER}@127.0.0.1:{s.port}"
    s.password = PASSWORD
    s.kind = request.param
    yield s
    s.stop()
    from pr7e import servers
    for k in list(servers.CONNECTED):
        servers.disconnect(k)
