import os
import sys
import time

import pytest

from pr7e import trash


def test_folder_trash_send_list_restore(tmp_path):
    b = trash.FolderTrash(str(tmp_path / "Trash"))
    f = tmp_path / "docs" / "report.txt"
    f.parent.mkdir()
    f.write_bytes(b"report")
    d = tmp_path / "docs" / "photos"
    (d / "inner").mkdir(parents=True)
    (d / "inner" / "a.jpg").write_bytes(b"12345")
    it = trash.send(str(f), b)
    assert not f.exists() and it.name == "report.txt" and it.original == str(f)
    trash.send(str(d), b)
    items = {i.name: i for i in trash.items(b)}
    assert set(items) == {"report.txt", "photos"}
    assert items["photos"].is_dir and items["photos"].size == 5
    assert abs(items["report.txt"].deleted - time.time()) < 5
    # Same name again: both kept.
    f.write_bytes(b"second")
    trash.send(str(f), b)
    assert sorted(i.name for i in trash.items(b)) == ["photos", "report.txt", "report.txt"]
    # Restore puts it back; a name that's taken now gets " (2)".
    first = min((i for i in trash.items(b) if i.name == "report.txt"), key=lambda i: i.info)
    assert trash.restore(first, b) == str(f)
    assert f.read_bytes() in (b"report", b"second")
    other = next(i for i in trash.items(b) if i.name == "report.txt")
    assert trash.restore(other, b) == str(tmp_path / "docs" / "report (2).txt")
    p = next(i for i in trash.items(b) if i.name == "photos")
    os.rename(tmp_path / "docs", tmp_path / "moved")
    assert trash.restore(p, b) == str(d)  # the folder is made again
    assert (d / "inner" / "a.jpg").read_bytes() == b"12345"
    assert trash.items(b) == []


def test_folder_trash_delete_and_empty(tmp_path):
    b = trash.FolderTrash(str(tmp_path / "Trash"))
    for n in ("a", "b", "c"):
        (tmp_path / n).write_bytes(b"x")
        trash.send(str(tmp_path / n), b)
    one = trash.items(b)[0]
    trash.delete(one, b)
    assert len(trash.items(b)) == 2 and not os.path.exists(one.data)
    trash.empty(b)
    assert trash.items(b) == [] and os.listdir(b.files) == [] and os.listdir(b.infos) == []


def test_recycle_bin_records(tmp_path):
    """$I / $R pairs as Windows writes them."""
    bin_ = tmp_path / "S-1-5-21-1"
    bin_.mkdir()
    (bin_ / "$RAB12CD.txt").write_bytes(b"hello")
    (bin_ / "$IAB12CD.txt").write_bytes(trash.make_i_file(r"C:\Users\me\Desktop\note.txt", 5, 1_790_000_000))
    (bin_ / "$RZZ99.dir").mkdir()
    (bin_ / "$IZZ99.dir").write_bytes(trash.make_i_file(r"D:\Music\Old", 0, 1_790_000_100))
    (bin_ / "$I-orphan.txt").write_bytes(trash.make_i_file(r"C:\gone.txt", 1, 1))  # its $R is gone: left out
    w = trash.WindowsTrash([str(bin_)])
    items = sorted(w.items(), key=lambda i: i.name)
    assert [(i.name, i.original, i.is_dir) for i in items] == [
        ("Old", r"D:\Music\Old", True), ("note.txt", r"C:\Users\me\Desktop\note.txt", False)]
    assert abs(items[1].deleted - 1_790_000_000) < 1e-3 and items[1].size == 5
    w.forget(items[1])
    assert not (bin_ / "$IAB12CD.txt").exists()


def test_version_1_records():
    import struct
    data = struct.pack("<qqq", 1, 7, 0) + "C:\\a.txt".encode("utf-16-le").ljust(520, b"\0")
    assert trash._parse_i_file(data) == ("C:\\a.txt", 0.0, 7)


@pytest.mark.skipif(sys.platform != "win32" or not os.environ.get("CI"), reason="the real Recycle Bin: CI only")
def test_real_recycle_bin(tmp_path):
    w = trash.WindowsTrash()
    f = tmp_path / f"pr7e-test-{os.getpid()}.txt"
    f.write_bytes(b"recycle me")
    try:
        it = w.send(str(f))
    except trash.TrashError as e:
        pytest.skip(f"no Recycle Bin here: {e}")
    assert not f.exists()
    assert any(i.info == it.info for i in w.items())
    assert trash.restore(it, w) == str(f)
    assert f.read_bytes() == b"recycle me"
