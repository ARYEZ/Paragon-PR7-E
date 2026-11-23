import os

import pytest

from pr7e import ops, trash


def make(root, files):
    for rel, data in files.items():
        p = root / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data)


def tree(root):
    out = {}
    for dirpath, dirs, files in os.walk(root):
        for f in files:
            p = os.path.join(dirpath, f)
            out[os.path.relpath(p, root).replace("\\", "/")] = open(p, "rb").read()
        for d in dirs:
            if not os.listdir(os.path.join(dirpath, d)):
                out[os.path.relpath(os.path.join(dirpath, d), root).replace("\\", "/") + "/"] = None
    return out


def run(job, answers=None):
    if answers is not None:
        it = iter(answers)
        job.ask = lambda c: next(it)
    job.run()
    return job


def test_copy_files_and_folders(tmp_path):
    src, dst = tmp_path / "src", tmp_path / "dst"
    make(src, {"a.txt": b"A" * 3000, "album/01.mp3": os.urandom(250_000), "album/art/cover.jpg": b"jpg"})
    (src / "album" / "empty").mkdir()
    dst.mkdir()
    os.utime(src / "a.txt", (1_700_000_000, 1_700_000_000))
    job = run(ops.CopyJob([str(src / "a.txt"), str(src / "album")], str(dst)))
    assert job.state == "done" and not job.errors
    assert tree(dst) == tree(src)
    assert os.path.getmtime(dst / "a.txt") == 1_700_000_000   # dates kept, as Files keeps them
    assert job.done_bytes == job.total_bytes == 3000 + 250_000 + 3
    assert job.fraction() == 1.0
    assert sorted(os.path.basename(p) for p in job.created) == ["a.txt", "album"]
    assert tree(src)  # the originals are still there


def test_paste_into_the_same_folder_makes_copies(tmp_path):
    make(tmp_path, {"photo.jpg": b"1", "Shoot/x.jpg": b"2"})
    run(ops.CopyJob([str(tmp_path / "photo.jpg"), str(tmp_path / "Shoot")], str(tmp_path)))
    run(ops.CopyJob([str(tmp_path / "photo.jpg")], str(tmp_path)))
    assert sorted(os.listdir(tmp_path)) == ["Shoot", "Shoot (copy)", "photo (copy 2).jpg", "photo (copy).jpg",
                                            "photo.jpg"]
    assert (tmp_path / "Shoot (copy)" / "x.jpg").read_bytes() == b"2"


def test_conflicts_replace_skip_rename_all(tmp_path):
    src, dst = tmp_path / "src", tmp_path / "dst"
    make(src, {"1.txt": b"new1", "2.txt": b"new2", "3.txt": b"new3", "4.txt": b"new4"})
    make(dst, {"1.txt": b"old1", "2.txt": b"old2", "3.txt": b"old3", "4.txt": b"old4"})
    asked = []

    def ask(c):
        asked.append(os.path.basename(c.target))
        return {"1.txt": ("replace", False), "2.txt": ("skip", False), "3.txt": ("rename", "3 new.txt"),
                "4.txt": ("replace", True)}[os.path.basename(c.target)]
    job = ops.CopyJob([str(src / n) for n in ("1.txt", "2.txt", "3.txt", "4.txt")], str(dst))
    job.ask = ask
    job.run()
    assert tree(dst) == {"1.txt": b"new1", "2.txt": b"old2", "3.txt": b"old3", "3 new.txt": b"new3",
                         "4.txt": b"new4"}
    assert asked == ["1.txt", "2.txt", "3.txt", "4.txt"]


def test_apply_to_all_and_merge(tmp_path):
    src, dst = tmp_path / "src", tmp_path / "dst"
    make(src, {"Album/01.mp3": b"n1", "Album/02.mp3": b"n2", "Album/03.mp3": b"n3", "b.txt": b"nb"})
    make(dst, {"Album/01.mp3": b"o1", "Album/02.mp3": b"o2", "Album/old.mp3": b"keep", "b.txt": b"ob"})
    questions = []

    def ask(c):
        questions.append((os.path.basename(c.target), c.merge))
        return ("merge", False) if c.merge else ("skip", True)
    job = ops.CopyJob([str(src / "Album"), str(src / "b.txt")], str(dst))
    job.ask = ask
    job.run()
    assert questions == [("Album", True), ("01.mp3", False)]  # "skip all" answered the rest
    assert tree(dst) == {"Album/01.mp3": b"o1", "Album/02.mp3": b"o2", "Album/03.mp3": b"n3",
                         "Album/old.mp3": b"keep", "b.txt": b"ob"}


def test_cancel_in_the_question_stops_and_leaves_no_half_files(tmp_path):
    src, dst = tmp_path / "src", tmp_path / "dst"
    make(src, {"a.txt": b"a", "b.txt": b"b"})
    make(dst, {"a.txt": b"old"})
    job = run(ops.CopyJob([str(src / "a.txt"), str(src / "b.txt")], str(dst)), [None])
    assert job.state == "cancelled"
    assert tree(dst) == {"a.txt": b"old"}


def test_cancel_mid_file(tmp_path):
    src = tmp_path / "big.bin"
    src.write_bytes(os.urandom(5 * ops.CHUNK))
    (tmp_path / "dst").mkdir()
    job = ops.CopyJob([str(src)], str(tmp_path / "dst"))
    real = job.check
    calls = []

    def check():
        calls.append(1)
        if len(calls) > 3:
            job.cancel()
        real()
    job.check = check
    job.run()
    assert job.state == "cancelled"
    assert os.listdir(tmp_path / "dst") == []


def test_move_same_drive_is_a_rename_and_undo_moves_back(tmp_path):
    make(tmp_path, {"src/Album/01.mp3": b"1", "src/a.txt": b"a"})
    (tmp_path / "dst").mkdir()
    ino = os.stat(tmp_path / "src" / "Album" / "01.mp3").st_ino
    job = run(ops.CopyJob([str(tmp_path / "src" / "Album"), str(tmp_path / "src" / "a.txt")],
                          str(tmp_path / "dst"), move=True))
    assert job.state == "done" and tree(tmp_path / "src") == {} and tree(tmp_path / "dst") == {
        "Album/01.mp3": b"1", "a.txt": b"a"}
    assert os.stat(tmp_path / "dst" / "Album" / "01.mp3").st_ino == ino   # renamed, not copied
    undo = job.undo
    assert isinstance(undo, ops.MovePairsJob)
    run(undo)
    assert tree(tmp_path / "src") == {"Album/01.mp3": b"1", "a.txt": b"a"} and tree(tmp_path / "dst") == {}
    run(undo.undo)  # redo
    assert tree(tmp_path / "dst") == {"Album/01.mp3": b"1", "a.txt": b"a"}


def test_move_merge_and_replace(tmp_path):
    make(tmp_path, {"src/Album/01.mp3": b"new", "src/Album/02.mp3": b"two", "dst/Album/01.mp3": b"old",
                    "dst/Album/keep.mp3": b"k"})
    job = run(ops.CopyJob([str(tmp_path / "src" / "Album")], str(tmp_path / "dst"), move=True),
              [("merge", False), ("replace", False)])
    assert tree(tmp_path / "dst") == {"Album/01.mp3": b"new", "Album/02.mp3": b"two", "Album/keep.mp3": b"k"}
    assert not (tmp_path / "src" / "Album").exists()
    assert not job.errors


def test_into_itself_is_refused(tmp_path):
    make(tmp_path, {"A/B/x.txt": b"x"})
    for move in (False, True):
        job = run(ops.CopyJob([str(tmp_path / "A")], str(tmp_path / "A" / "B"), move=move))
        assert job.errors and "into itself" in job.errors[0]
    assert tree(tmp_path) == {"A/B/x.txt": b"x"}


def test_cross_drive_move_falls_back_to_copy_and_delete(tmp_path, monkeypatch):
    make(tmp_path, {"src/Album/01.mp3": b"1", "src/b.txt": b"b"})
    (tmp_path / "dst").mkdir()
    monkeypatch.setattr(ops.fs, "same_volume", lambda a, b: False)
    job = run(ops.CopyJob([str(tmp_path / "src" / "Album"), str(tmp_path / "src" / "b.txt")],
                          str(tmp_path / "dst"), move=True))
    assert tree(tmp_path / "dst") == {"Album/01.mp3": b"1", "b.txt": b"b"} and tree(tmp_path / "src") == {}
    assert job.total_bytes == 2 and job.done_bytes == 2


def test_trash_undo_redo(tmp_path):
    make(tmp_path, {"f/a.txt": b"a", "f/Dir/b.txt": b"b"})
    job = run(ops.TrashJob([str(tmp_path / "f" / "a.txt"), str(tmp_path / "f" / "Dir")]))
    assert tree(tmp_path / "f") == {}
    assert sorted(i.name for i in trash.items()) == ["Dir", "a.txt"]
    restore = job.undo
    assert isinstance(restore, ops.RestoreJob)
    run(restore)
    assert tree(tmp_path / "f") == {"a.txt": b"a", "Dir/b.txt": b"b"} and trash.items() == []
    run(restore.undo)  # redo: back to Trash
    assert tree(tmp_path / "f") == {} and len(trash.items()) == 2


def test_copy_undo_trashes_the_copies_and_redo_restores_them(tmp_path):
    make(tmp_path, {"src/a.txt": b"a"})
    (tmp_path / "dst").mkdir()
    job = run(ops.CopyJob([str(tmp_path / "src" / "a.txt")], str(tmp_path / "dst")))
    run(job.undo)
    assert os.listdir(tmp_path / "dst") == [] and (tmp_path / "src" / "a.txt").exists()
    run(job.undo.undo)
    assert (tmp_path / "dst" / "a.txt").read_bytes() == b"a"


def test_delete_for_good(tmp_path):
    make(tmp_path, {"a.txt": b"a", "D/b.txt": b"b"})
    os.chmod(tmp_path / "D" / "b.txt", 0o444)  # read-only files go too
    job = run(ops.DeleteJob([str(tmp_path / "a.txt"), str(tmp_path / "D")]))
    assert not job.errors and [n for n in os.listdir(tmp_path) if n != "_trash"] == []
    assert job.undo is None and trash.items() == []


def test_rename_and_new_folder(tmp_path):
    make(tmp_path, {"old.txt": b"o", "taken.txt": b"t"})
    job = run(ops.RenameJob(str(tmp_path / "old.txt"), "new.txt"))
    assert (tmp_path / "new.txt").read_bytes() == b"o"
    run(job.undo)
    assert (tmp_path / "old.txt").exists() and not (tmp_path / "new.txt").exists()
    bad = run(ops.RenameJob(str(tmp_path / "old.txt"), "taken.txt"))
    assert bad.errors and (tmp_path / "taken.txt").read_bytes() == b"t"
    nf = run(ops.NewFolderJob(str(tmp_path), "Projects"))
    assert (tmp_path / "Projects").is_dir()
    run(nf.undo)
    assert not (tmp_path / "Projects").exists() and [i.name for i in trash.items()] == ["Projects"]


def test_progress_text(tmp_path):
    job = ops.CopyJob([str(tmp_path)], str(tmp_path / "x"))
    job.total_bytes, job.done_bytes = 1_200_000_000, 412_600_000
    job.started = __import__("time").monotonic() - 10
    job.state = "running"
    d = job.detail()
    assert d.startswith("412.6 MB of 1.2 GB — ") and d.endswith("seconds left")
    assert ops._left(1) == "1 second left" and ops._left(95) == "2 minutes left" and ops._left(7300) == \
        "2 hours left"
    t = ops.TrashJob(["/a", "/b", "/c"])
    t.done_items = 2
    assert t.detail() == "2 of 3 files" and t.title == "Moving 3 files to Trash"
