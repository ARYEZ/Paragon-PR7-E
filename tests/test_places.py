import os
import sys

import pytest

from pr7e import places


def test_lnk_target_unicode_and_ansi(tmp_path):
    assert places.lnk_target(places.make_lnk(r"C:\Users\Ünïcode\Music\a b.mp3")) == r"C:\Users\Ünïcode\Music\a b.mp3"
    # An ANSI-only LinkInfo (header 0x1C), behind a target ID list, as older shortcuts have.
    import struct
    import uuid
    header = struct.pack("<I16sI", 0x4C, uuid.UUID("00021401-0000-0000-C000-000000000046").bytes_le, 0x1 | 0x2)
    header += b"\0" * (0x4C - len(header))
    idlist = struct.pack("<H", 6) + b"\x04\x00ab\x00\x00"
    base = b"C:\\Data\\\0"
    suffix = b"report.pdf\0"
    li_body = base + suffix
    li = struct.pack("<7I", 0x1C + len(li_body), 0x1C, 0x1, 0, 0x1C, 0, 0x1C + len(base)) + li_body
    assert places.lnk_target(header + idlist + li) == r"C:\Data\report.pdf"
    assert places.lnk_target(b"junk") is None


def test_recent_from_shortcuts(tmp_path):
    recent = tmp_path / "Recent"
    recent.mkdir()
    a = tmp_path / "a.txt"
    a.write_text("a")
    b = tmp_path / "b.mp3"
    b.write_text("b")
    (recent / "a.txt.lnk").write_bytes(places.make_lnk(str(a)))
    (recent / "b.mp3.lnk").write_bytes(places.make_lnk(str(b)))
    (recent / "gone.lnk").write_bytes(places.make_lnk(str(tmp_path / "gone.txt")))
    (recent / "folder.lnk").write_bytes(places.make_lnk(str(tmp_path)))
    os.utime(recent / "a.txt.lnk", (1_700_000_000, 1_700_000_000))
    assert [p for p, _t in places.recent(folder=str(recent))] == [str(b), str(a)]


@pytest.mark.skipif(sys.platform == "win32", reason="Linux: GTK's recent list")
def test_recent_xbel(tmp_path, monkeypatch):
    f = tmp_path / "song one.mp3"
    f.write_text("x")
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path))
    (tmp_path / "recently-used.xbel").write_text(
        '<?xml version="1.0"?><xbel version="1.0"><bookmark href="file://%s" visited="2026-11-20T10:00:00Z"/>'
        '<bookmark href="file:///nope.txt" visited="2026-11-21T10:00:00Z"/></xbel>' % str(f).replace(" ", "%20"))
    assert [p for p, _t in places.recent()] == [str(f)]


def test_bookmarks_and_stars(clean_settings, tmp_path):
    assert places.add_bookmark(str(tmp_path), "Work")
    assert not places.add_bookmark(str(tmp_path))
    assert places.bookmarks() == [{"path": str(tmp_path), "name": "Work"}]
    side = places.sidebar()
    assert [p.label for p in side[:3]] == ["Recent", "Starred", "Home"]
    assert any(p.kind == "bookmark" and p.label == "Work" for p in side)
    assert side[-1].path == "other:" and any(p.path == "trash:" for p in side)
    places.remove_bookmark(str(tmp_path))
    assert places.bookmarks() == []
    a, d = str(tmp_path / "a.txt"), str(tmp_path / "Dir")
    places.set_starred([a, d], True)
    assert places.is_starred(a) and places.is_starred(d)
    places.follow_star(d, str(tmp_path / "Renamed"))
    assert places.starred() == [a, str(tmp_path / "Renamed")]
    places.set_starred([a], False)
    assert places.starred() == [str(tmp_path / "Renamed")]


def test_drives():
    ds = places.drives()
    assert ds and all(d.root and d.label for d in ds)
    assert places.drive_of(ds[0].root + "x", ds) is ds[0] or places.drive_of(ds[0].root, ds) is not None


@pytest.mark.skipif(sys.platform != "win32", reason="Windows' known folders")
def test_known_folders():
    names = [label for label, _icon, _path in places.home_folders()]
    assert "Documents" in names


def test_network_share_addresses(clean_settings):
    assert places.share_path(r"\\nas\music\Paragon") == r"\\nas\music\Paragon"
    assert places.share_path("smb://nas/music/Paragon") == r"\\nas\music\Paragon"
    assert places.share_path("smb://me@nas/music") == r"\\nas\music"
    assert places.share_path("//nas/music") == r"\\nas\music"
    assert places.share_path(r"\\nas") is None and places.share_path("sftp://x") is None
    assert places.share_root(r"\\nas\music\a\b") == r"\\nas\music"
    assert places.share_label(r"\\nas\music") == "music on nas"
    assert places.is_remote(r"\\nas\music")
    places.add_share(r"\\nas\music\Paragon")
    places.add_share(r"\\NAS\Music")       # the same share: once
    assert places.shares() == [{"path": r"\\NAS\Music", "name": "Music on NAS"}]
    assert any(p.kind == "share" and p.eject for p in places.sidebar())
    places.remove_share(r"\\nas\music")
    assert places.shares() == []
