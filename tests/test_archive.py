"""Compress and Extract on real files: every format round-trips byte for byte, with and without 7-Zip."""
import os
import shutil
import zipfile

import pytest

from pr7e import archive, ops, trash

SEVEN = archive.find_7z() or shutil.which("7zz") or (
    "/tmp/claude-0/7zip-dl/linux/7zz" if os.path.isfile("/tmp/claude-0/7zip-dl/linux/7zz") else None)


def tree(root):
    out = {}
    for dirpath, dirs, files in os.walk(root):
        for f in files:
            p = os.path.join(dirpath, f)
            out[os.path.relpath(p, root).replace("\\", "/")] = open(p, "rb").read()
    return out


@pytest.fixture
def src(tmp_path):
    s = tmp_path / "src"
    (s / "Album" / "art").mkdir(parents=True)
    (s / "Album" / "01 Portal.flac").write_bytes(os.urandom(300_000))
    (s / "Album" / "art" / "cover.jpg").write_bytes(b"jpg" * 1000)
    (s / "notes ü.txt").write_bytes("notes ü".encode())
    return s


@pytest.fixture
def seven(monkeypatch):
    if not SEVEN:
        pytest.skip("no 7-Zip here")
    monkeypatch.setenv("PR7E_7Z", SEVEN)


@pytest.fixture
def no_seven(monkeypatch):
    monkeypatch.setenv("PR7E_7Z", os.devnull + "-missing")


def run(job, password=None, answers=None):
    if hasattr(job, "ask_password"):
        it = iter(password if isinstance(password, list) else [password])
        job.ask_password = lambda name: next(it)
    job.run()
    return job


def roundtrip(src, tmp_path, fmt, password=""):
    out = tmp_path / "out"
    out.mkdir(parents=True, exist_ok=True)
    job = run(archive.CompressJob([str(src / "Album"), str(src / "notes ü.txt")], str(out), "Archive", fmt,
                                  password))
    assert job.state == "done" and not job.errors, job.errors
    assert os.listdir(out) == [os.path.basename(job.target)]   # no part files left
    assert job.done_bytes == job.total_bytes
    ex = tmp_path / "ex"
    ex.mkdir(parents=True, exist_ok=True)
    e = run(archive.ExtractJob(job.target, str(ex)), password or None)
    assert e.state == "done" and not e.errors, e.errors
    # Several items at the top: Files puts them in a folder named after the archive.
    assert os.listdir(ex) == ["Archive"]
    assert tree(ex / "Archive") == tree(src)
    return job


def test_zip_with_python(src, tmp_path, no_seven):
    job = roundtrip(src, tmp_path, ".zip")
    with zipfile.ZipFile(job.target) as z:
        assert "Album/art/cover.jpg" in z.namelist() and "notes ü.txt" in z.namelist()


def test_tar_xz_with_python(src, tmp_path, no_seven):
    job = roundtrip(src, tmp_path, ".tar.xz")
    assert job.target.endswith("Archive.tar.xz")


def test_7z_and_zip_with_7zip(src, tmp_path, seven):
    roundtrip(src, tmp_path / "a", ".7z")
    roundtrip(src, tmp_path / "b", ".zip")
    roundtrip(src, tmp_path / "c", ".tar.xz")


def test_encrypted_archives(src, tmp_path, seven):
    job = roundtrip(src, tmp_path / "z", "Encrypted .zip", "s3cret")
    with zipfile.ZipFile(job.target) as z:
        assert all(i.flag_bits & 0x1 for i in z.infolist() if not i.is_dir())
    roundtrip(src, tmp_path / "s", ".7z", "s3cret")
    # A wrong password is asked again; cancelling the question cancels the job.
    ex = tmp_path / "again"
    e = run(archive.ExtractJob(str(tmp_path / "s" / "out" / "Archive.7z"), str(ex)), ["nope", "s3cret"])
    assert e.state == "done" and tree(ex / "Archive") == tree(src)
    e = run(archive.ExtractJob(str(tmp_path / "s" / "out" / "Archive.7z"), str(tmp_path / "c")), [None])
    assert e.state == "cancelled" and [n for n in os.listdir(tmp_path / "c") if not n.startswith(".")] == []


def test_one_item_extracts_as_itself_and_names_dont_clash(src, tmp_path, no_seven):
    out = tmp_path / "out"
    out.mkdir()
    job = run(archive.CompressJob([str(src / "Album")], str(out), "Album", ".zip"))
    for _ in range(2):
        run(archive.ExtractJob(job.target))
    assert sorted(os.listdir(out)) == ["Album", "Album (2)", "Album.zip"]
    assert tree(out / "Album (2)") == tree(src / "Album")


def test_undo_trashes_what_was_made(src, tmp_path, no_seven):
    out = tmp_path / "out"
    out.mkdir()
    job = run(archive.CompressJob([str(src / "notes ü.txt")], str(out), "notes", ".zip"))
    e = run(archive.ExtractJob(job.target, str(tmp_path / "x")))
    run(e.undo)
    assert os.listdir(tmp_path / "x") == []
    run(job.undo)
    assert os.listdir(out) == [] and sorted(i.name for i in trash.items()) == ["notes ü.txt", "notes.zip"]


def test_existing_name_refused(src, tmp_path, no_seven):
    (tmp_path / "Archive.zip").write_bytes(b"keep")
    job = run(archive.CompressJob([str(src / "Album")], str(tmp_path), "Archive", ".zip"))
    assert job.errors and (tmp_path / "Archive.zip").read_bytes() == b"keep"


def test_without_7zip_rar_says_what_it_needs(tmp_path, no_seven):
    (tmp_path / "a.rar").write_bytes(b"Rar!")
    job = run(archive.ExtractJob(str(tmp_path / "a.rar")))
    assert job.errors and "PR7-Z" in job.errors[0]


def test_cancel_leaves_nothing(src, tmp_path, no_seven):
    big = tmp_path / "big.bin"
    big.write_bytes(os.urandom(6 * ops.CHUNK))
    job = archive.CompressJob([str(big)], str(tmp_path), "big", ".zip")
    real = job.check
    n = []

    def check():
        n.append(1)
        if len(n) > 4:
            job.cancel()
        real()
    job.check = check
    job.run()
    assert job.state == "cancelled"
    assert sorted(n for n in os.listdir(tmp_path) if n != "_trash") == ["big.bin", "src"]  # no part file left


def test_names():
    assert archive.default_name(["/a/notes.txt"]) == "notes" and archive.default_name(["/a", "/b"]) == "Archive"
    assert archive.archive_stem("Photos.tar.gz") == "Photos"
    assert archive.can_extract("x.RAR") and not archive.can_extract("x.txt")
