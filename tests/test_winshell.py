"""The Windows shell through COM (Windows only: runs on CI)."""
import sys

import pytest

pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="the Windows shell")


def test_the_windows_menu_has_explorers_items(tmp_path):
    from pr7e import winshell
    a = tmp_path / "a.txt"
    a.write_text("a")
    b = tmp_path / "b.txt"
    b.write_text("b")
    items = winshell.menu_items([str(a)])
    assert "Properties" in items, items
    assert any(i.strip() in ("Open", "Edit", "Print") for i in items), items
    two = winshell.menu_items([str(a), str(b)])
    assert "Properties" in two
    assert winshell.same_folder([str(a), str(b)]) and not winshell.same_folder([str(a), str(tmp_path.parent)])


def test_windows_makes_thumbnails(qapp, tmp_path):
    from PySide6.QtGui import QColor, QImage
    from pr7e import winshell
    from pr7e.workers import _ShellThumb, _Signals
    img = QImage(400, 300, QImage.Format_RGB32)
    img.fill(QColor("#DE0639"))
    p = tmp_path / "red.png"
    img.save(str(p))
    got = winshell.thumbnail(str(p), 128)
    assert got is not None
    w, h, data, _alpha = got
    assert max(w, h) >= 128 and len(data) == w * h * 4
    centre = (h // 2 * w + w // 2) * 4
    b, g, r = data[centre], data[centre + 1], data[centre + 2]
    assert r > 180 and g < 60 and b < 100     # the picture's own red, not an icon
    seen = []
    sig = _Signals()
    sig.thumb.connect(lambda path, mtime, im: seen.append(im))
    _ShellThumb(str(p), 0.0, 128, sig).run()
    assert seen and seen[0] is not None and not seen[0].isNull()
    (tmp_path / "plain.unknownext").write_text("x")
    assert winshell.thumbnail(str(tmp_path / "plain.unknownext"), 128) is None  # icon only: no thumbnail
