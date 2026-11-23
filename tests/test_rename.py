import os
import time

from pr7e import rename


def make(tmp_path, names):
    now = time.time()
    for i, n in enumerate(names):
        p = tmp_path / n
        p.write_text(n)
        os.utime(p, (now - (len(names) - i) * 60,) * 2)   # created in this order
    return [str(tmp_path / n) for n in names]


def test_template_numbers_in_name_order_and_keeps_extensions(tmp_path):
    paths = make(tmp_path, ["DSC_0418.jpg", "DSC_0412.jpg", "DSC_0421.jpg", "notes.txt"])
    items = rename.from_template(paths, "Elseworld [01, 02, 03]")
    assert [(i.name, i.new) for i in items] == [("DSC_0412.jpg", "Elseworld 01.jpg"), ("DSC_0418.jpg", "Elseworld 02.jpg"),
                                                ("DSC_0421.jpg", "Elseworld 03.jpg"), ("notes.txt", "Elseworld 04.txt")]
    assert rename.ready(items)
    items = rename.from_template(paths, "[1, 2, 3] - [Original file name]", "mtime")
    assert [i.new for i in items] == ["1 - DSC_0418.jpg", "2 - DSC_0412.jpg", "3 - DSC_0421.jpg", "4 - notes.txt"]
    assert rename.from_template(paths, "x [001, 002, 003]", "name_desc")[0].new == "x 001.txt"


def test_find_replace(tmp_path):
    paths = make(tmp_path, ["01 Portal (demo).flac", "02 Ruins (demo).flac", "cover.jpg"])
    items = rename.find_replace(paths, " (demo)", "")
    assert [i.new for i in items] == ["01 Portal.flac", "02 Ruins.flac", "cover.jpg"]
    assert rename.ready(items)
    assert not rename.ready(rename.find_replace(paths, "zzz", "y"))   # nothing changes


def test_problems_are_marked(tmp_path):
    paths = make(tmp_path, ["a.txt", "b.txt"])
    (tmp_path / "taken.txt").write_text("t")
    same = rename.from_template(paths, "same")
    assert all(i.problem == "Another file would get the same name." for i in same) and not rename.ready(same)
    taken = rename.find_replace(paths[:1], "a", "taken")
    assert taken[0].problem == "A file with that name already exists."
    bad = rename.find_replace(paths[:1], "a", "a:b")
    assert "cannot contain" in bad[0].problem
    empty = rename.from_template(paths, "")
    assert empty[0].problem == "The name is empty."


def test_swaps_and_undo(tmp_path):
    a, b = make(tmp_path, ["a.txt", "b.txt"])
    items = rename.check([rename.Item(a, "b.txt"), rename.Item(b, "a.txt")])
    assert rename.ready(items)   # taken by an item that's moving out of the way
    job = rename.BatchRenameJob([(i.path, i.new) for i in items])
    job.run()
    assert (tmp_path / "a.txt").read_text() == "b.txt" and (tmp_path / "b.txt").read_text() == "a.txt"
    job.undo.run()
    assert (tmp_path / "a.txt").read_text() == "a.txt" and (tmp_path / "b.txt").read_text() == "b.txt"
    assert sorted(n for n in os.listdir(tmp_path) if n != "_trash") == ["a.txt", "b.txt"]  # no temporary names left


def test_job_refuses_when_something_changed_meanwhile(tmp_path):
    (a,) = make(tmp_path, ["a.txt"])
    (tmp_path / "late.txt").write_text("late")
    job = rename.BatchRenameJob([(a, "late.txt")])
    job.run()
    assert job.errors and (tmp_path / "late.txt").read_text() == "late" and (tmp_path / "a.txt").exists()
