import os
import sys
import time

import pytest

from pr7e import fs


def touch(path, data=b"x", mtime=None):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    if mtime:
        os.utime(path, (mtime, mtime))
    return path


def test_listing_sorts_folders_first_and_numbers_by_value(tmp_path):
    for n in ("track 10.mp3", "track 2.mp3", "Alpha.txt", "beta.txt", "Café.txt"):
        touch(tmp_path / n)
    (tmp_path / "zeta").mkdir()
    (tmp_path / "Archive").mkdir()
    names = [e.name for e in fs.sort_entries(fs.list_dir(str(tmp_path)))]
    assert names == ["Archive", "zeta", "Alpha.txt", "beta.txt", "Café.txt", "track 2.mp3", "track 10.mp3"]


def test_sort_orders(tmp_path):
    now = time.time()
    touch(tmp_path / "old.txt", b"a" * 50, now - 1000)
    touch(tmp_path / "new.txt", b"a" * 5, now)
    touch(tmp_path / "mid.txt", b"a" * 500, now - 500)
    (tmp_path / "dir").mkdir()
    es = fs.list_dir(str(tmp_path))
    assert [e.name for e in fs.sort_entries(es, "mtime_desc")][1:] == ["new.txt", "mid.txt", "old.txt"]
    assert [e.name for e in fs.sort_entries(es, "size_desc")][1:] == ["mid.txt", "old.txt", "new.txt"]
    assert [e.name for e in fs.sort_entries(es, "name_desc")] == ["dir", "old.txt", "new.txt", "mid.txt"]
    assert fs.sort_entries(es, "size")[0].name == "dir"


def test_hidden_files(tmp_path):
    touch(tmp_path / ".secret")
    touch(tmp_path / "plain.txt")
    assert [e.name for e in fs.list_dir(str(tmp_path))] == ["plain.txt"]
    assert sorted(e.name for e in fs.list_dir(str(tmp_path), show_hidden=True)) == [".secret", "plain.txt"]


@pytest.mark.skipif(sys.platform != "win32", reason="Windows' hidden attribute")
def test_windows_hidden_attribute(tmp_path):
    import ctypes
    p = touch(tmp_path / "hidden.txt")
    ctypes.windll.kernel32.SetFileAttributesW(str(p), 0x2)
    assert fs.list_dir(str(tmp_path)) == []


def test_kinds():
    assert fs.kind_of("a.MP3") == "audio"
    assert fs.kind_of("photo.jpeg") == "image"
    assert fs.kind_of("x.7z") == "archive"
    assert fs.kind_of("doc.pdf") == "document"
    assert fs.kind_of("README") == "file"
    assert fs.kind_of("Music", True) == "folder"


def test_search_words_kinds_dates_and_locations(tmp_path):
    now = time.time()
    touch(tmp_path / "Music" / "Paragon" / "ELSEWORLD" / "01 Portal.flac", mtime=now - 3600)
    touch(tmp_path / "Music" / "Paragon" / "ELSEWORLD" / "02 Café Elsewhere.mp3", mtime=now - 40 * 86400)
    touch(tmp_path / "Documents" / "elseworld notes.txt", mtime=now - 3600)
    (tmp_path / "Music" / "Paragon" / "ELSEWORLD demos").mkdir()
    found = {e.name: e.location for e in fs.search(str(tmp_path), "elsew")}
    assert found == {"02 Café Elsewhere.mp3": "Music/Paragon/ELSEWORLD", "elseworld notes.txt": "Documents",
                     "ELSEWORLD": "Music/Paragon", "ELSEWORLD demos": "Music/Paragon"}
    assert [e.name for e in fs.search(str(tmp_path), "cafe elsewhere")] == ["02 Café Elsewhere.mp3"]
    assert sorted(e.name for e in fs.search(str(tmp_path), "", what="Music")) == ["01 Portal.flac",
                                                                                 "02 Café Elsewhere.mp3"]
    assert sorted(e.name for e in fs.search(str(tmp_path), "", when="Last 7 days", what="Music")) == [
        "01 Portal.flac"]
    assert sorted(e.name for e in fs.search(str(tmp_path), "elseworld", what="Folders")) == [
        "ELSEWORLD", "ELSEWORLD demos"]
    assert list(fs.search(str(tmp_path), "")) == []


def test_search_can_be_stopped(tmp_path):
    for i in range(50):
        touch(tmp_path / f"d{i}" / "match.txt")
    seen = []
    for e in fs.search(str(tmp_path), "match", cancelled=lambda: len(seen) >= 3):
        seen.append(e)
    assert len(seen) < 50


def test_wording():
    assert fs.human_size(1) == "1 byte"
    assert fs.human_size(999) == "999 bytes"
    assert fs.human_size(4_200_000) == "4.2 MB"
    assert fs.human_size(1_884_120_334) == "1.9 GB"
    assert fs.items_text(1) == "1 item" and fs.items_text(12) == "12 items"
    now = time.mktime((2026, 11, 23, 15, 0, 0, 0, 0, -1))
    assert fs.date_text(now - 3600, now) == "14:00"
    assert fs.date_text(now - 86400, now) == "Yesterday"
    assert fs.date_text(now - 3 * 86400, now) == time.strftime("%A", time.localtime(now - 3 * 86400))
    assert fs.date_text(now - 30 * 86400, now) == "24 Oct 2026"


def test_names(tmp_path):
    touch(tmp_path / "photo.jpg")
    (tmp_path / "Folder").mkdir()
    assert fs.copy_name("photo.jpg", str(tmp_path)) == "photo (copy).jpg"
    touch(tmp_path / "photo (copy).jpg")
    assert fs.copy_name("photo.jpg", str(tmp_path)) == "photo (copy 2).jpg"
    assert fs.copy_name("photo (copy).jpg", str(tmp_path)) == "photo (copy 2).jpg"
    assert fs.copy_name("Folder", str(tmp_path), True) == "Folder (copy)"
    assert fs.split_ext("backup.tar.gz") == ("backup", ".tar.gz")
    assert fs.split_ext(".bashrc") == (".bashrc", "")
    assert fs.free_name("photo.jpg", str(tmp_path)) == "photo (2).jpg"
    assert fs.name_problem("photo.jpg", str(tmp_path)) == "A file with that name already exists."
    assert fs.name_problem("Folder", str(tmp_path), True) == "A folder with that name already exists."
    assert "cannot contain" in fs.name_problem("a:b", str(tmp_path))
    assert "space or a full stop" in fs.name_problem("name.", str(tmp_path))
    assert "Windows keeps" in fs.name_problem("con.txt", str(tmp_path))
    assert fs.name_problem("photo.jpg", str(tmp_path), current="photo.jpg") == ""
    assert fs.name_problem("new.jpg", str(tmp_path)) == ""
