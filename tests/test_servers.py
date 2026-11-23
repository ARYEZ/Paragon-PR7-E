"""Connect to Server against real FTP, FTP-over-TLS and SFTP servers on 127.0.0.1."""
import os

import pytest

from pr7e import remote, servers


def connect(server):
    site, path = servers.parse(server.address)
    site.password = server.password
    return servers.connect(site, hostkey=lambda *a: "always"), path


def test_locations():
    assert servers.split("sftp://me@host:22/a/b") == ("sftp://me@host:22", "/a/b")
    assert servers.split("ftp://host:21") == ("ftp://host:21", "/")
    site, path = servers.parse("example.com/pub")
    assert site.protocol == "ftp" and site.host == "example.com" and path == "/pub"
    assert servers.key_for(site) == "ftp://example.com:21"
    site, _ = servers.parse("sftp://me@10.0.0.2")
    assert servers.key_for(site) == "sftp://me@10.0.0.2:22"
    assert servers.is_remote("ftpes://x:21/") and not servers.is_remote("C:\\Users")


def test_browse_transfer_and_manage(server, server_root, tmp_path):
    srv, _path = connect(server)
    assert srv.key in servers.CONNECTED
    names = {e.name: e for e in srv.list("/")}
    assert set(names) == {"index.html", "music"} and names["music"].is_dir
    assert names["index.html"].path == srv.key + "/index.html"
    # Download a folder and a file.
    local = tmp_path / "local"
    local.mkdir()
    job = servers.TransferJob([srv.key + "/music", srv.key + "/index.html"], str(local))
    job.run()
    assert job.state == "done" and not job.errors, job.errors
    assert (local / "music" / "elseworld" / "01 Portal.flac").read_bytes() == \
        (server_root / "music" / "elseworld" / "01 Portal.flac").read_bytes()
    assert job.done_bytes == job.total_bytes == 300_000 + len(b"server notes") + len(b"<html>hi</html>")
    # Upload: a clash asks Files' question.
    (local / "index.html").write_bytes(b"new page")
    up = servers.TransferJob([str(local / "index.html")], srv.key + "/")
    asked = []
    up.ask = lambda c: (asked.append((c.target, c.source_size, c.target_size)), ("replace", False))[1]
    up.run()
    assert not up.errors and asked == [("/index.html", 8, 15)]
    assert (server_root / "index.html").read_bytes() == b"new page"
    (local / "folder up" / "inner").mkdir(parents=True)
    (local / "folder up" / "inner" / "x.bin").write_bytes(os.urandom(70_000))
    servers.TransferJob([str(local / "folder up")], srv.key + "/music").run()
    assert (server_root / "music" / "folder up" / "inner" / "x.bin").read_bytes() == \
        (local / "folder up" / "inner" / "x.bin").read_bytes()
    # New folder, rename (and undo), delete.
    nf = servers.NewFolderJob(srv.key + "/", "Made here")
    nf.run()
    assert (server_root / "Made here").is_dir()
    rn = servers.RenameJob(srv.key + "/Made here", "Renamed")
    rn.run()
    assert (server_root / "Renamed").is_dir() and not (server_root / "Made here").exists()
    rn.undo.run()
    assert (server_root / "Made here").is_dir()
    servers.DeleteJob([srv.key + "/Made here", srv.key + "/music/folder up"]).run()
    assert not (server_root / "Made here").exists() and not (server_root / "music" / "folder up").exists()
    # Open: downloaded to the temporary folder.
    op = servers.OpenJob(srv.key + "/music/notes.txt")
    op.run()
    assert open(op.local, "rb").read() == b"server notes"
    servers.disconnect(srv.key)
    assert srv.key not in servers.CONNECTED


def test_move_from_server_deletes_there(server, server_root, tmp_path):
    srv, _ = connect(server)
    job = servers.TransferJob([srv.key + "/music/notes.txt"], str(tmp_path), move=True)
    job.run()
    assert (tmp_path / "notes.txt").read_bytes() == b"server notes" and not (server_root / "music" / "notes.txt").exists()


def test_reconnects_when_the_connection_drops(server):
    srv, _ = connect(server)
    srv.session.close()
    assert {e.name for e in srv.list("/")} == {"index.html", "music"}


def test_wrong_password(server):
    site, _ = servers.parse(server.address)
    site.password = "wrong"
    with pytest.raises(remote.RemoteError):
        servers.connect(site, hostkey=lambda *a: "always")


def test_passwords_only_in_the_keyring(qapp, clean_settings):
    servers.save_password("sftp://me@h:22", "pw")
    assert servers.saved_password("sftp://me@h:22") == "pw"
    servers.save_password("sftp://me@h:22", None)
    assert servers.saved_password("sftp://me@h:22") is None
    servers.add_recent("sftp://me@h:22")
    servers.add_recent("ftp://x:21")
    assert servers.recent() == ["ftp://x:21", "sftp://me@h:22"]
