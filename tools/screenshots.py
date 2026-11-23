"""Screenshots of the real window (offscreen) on a sample home folder: python tools/screenshots.py OUT_DIR"""
import json
import os
import random
import sys
import tempfile
import time
from pathlib import Path

_cfg = Path(tempfile.gettempdir()) / "pr7e_offscreen.json"
_cfg.write_text(json.dumps({"screens": [{"name": "s", "x": 0, "y": 0, "width": 3200, "height": 2200,
                                          "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1}]}))
os.environ.setdefault("QT_QPA_PLATFORM", f"offscreen:configfile={_cfg}")
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
work = Path(tempfile.mkdtemp())
os.environ["PR7E_TRASH"] = str(work / "Trash")

from PySide6.QtCore import QPointF, QRectF, QSettings, Qt  # noqa: E402
from PySide6.QtGui import QColor, QImage, QLinearGradient, QPainter, QPainterPath, QRadialGradient  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

QSettings.setDefaultFormat(QSettings.IniFormat)
QSettings.setPath(QSettings.IniFormat, QSettings.UserScope, str(work / "settings"))
from pr7e import ops, places, theme, trash  # noqa: E402

out = Path(sys.argv[1] if len(sys.argv) > 1 else "docs/screenshots")
out.mkdir(parents=True, exist_ok=True)
app = QApplication([])
theme.load_fonts()


def photo(seed: int, w: int = 900, h: int = 600) -> QImage:
    """A stand-in photo in family colours: a dusk sky, a sun, layered hills."""
    rnd = random.Random(seed)
    img = QImage(w, h, QImage.Format_RGB32)
    p = QPainter(img)
    p.setRenderHint(QPainter.Antialiasing)
    sky = QLinearGradient(0, 0, 0, h)
    sky.setColorAt(0, QColor(rnd.choice(["#2A0A1E", "#1A0610", "#3B0B1A", "#120A10"])))
    sky.setColorAt(0.65, QColor(rnd.choice(["#C2183F", "#E2502A", "#D83A6A", "#F07A2A"])))
    sky.setColorAt(1, QColor("#FFB070"))
    p.fillRect(0, 0, w, h, sky)
    sx, sy, sr = rnd.uniform(.2, .8) * w, rnd.uniform(.35, .6) * h, rnd.uniform(.08, .16) * h
    glow = QRadialGradient(QPointF(sx, sy), sr * 3)
    glow.setColorAt(0, QColor(255, 220, 180, 200))
    glow.setColorAt(1, QColor(255, 120, 60, 0))
    p.fillRect(0, 0, w, h, glow)
    p.setPen(Qt.NoPen)
    p.setBrush(QColor("#FFE2C2"))
    p.drawEllipse(QPointF(sx, sy), sr, sr)
    for layer, colour in enumerate(("#5A1426", "#33101A", "#160A0E")):
        path = QPainterPath(QPointF(0, h))
        base = h * (.62 + .12 * layer)
        x = 0.0
        while x <= w:
            path.lineTo(x, base - rnd.uniform(0, h * .16))
            x += w / rnd.randint(4, 9)
        path.lineTo(w, h)
        path.closeSubpath()
        p.fillPath(path, QColor(colour))
    p.end()
    return img


# ------------------------------------------------------------------ the sample home folder
home = work / "home"
now = time.time()


def put(rel, size=0, age_h=1.0, data=None):
    f = home / rel
    f.parent.mkdir(parents=True, exist_ok=True)
    if data is not None:
        f.write_bytes(data)
    else:
        with open(f, "wb") as fh:
            fh.truncate(size)
    t = now - age_h * 3600
    os.utime(f, (t, t))
    return f


for d in ("Desktop", "Documents", "Downloads", "Music", "Pictures", "Videos"):
    (home / d).mkdir(parents=True, exist_ok=True)
shoot = home / "Pictures" / "Elseworld shoot"
(shoot / "Contact sheets").mkdir(parents=True)
(shoot / "Edits").mkdir()
for i, n in enumerate(("DSC_0412", "DSC_0418", "DSC_0421", "DSC_0433", "DSC_0437", "DSC_0440", "DSC_0452",
                       "DSC_0459", "Ridge at dusk", "Static Bloom cover", "DSC_0466")):
    img = photo(i, 600, 900) if i in (3, 7) else photo(i)
    f = shoot / (n + (".png" if " " in n else ".jpg"))
    img.save(str(f), quality=88)
    os.utime(f, (now - (i + 1) * 5 * 3600,) * 2)
put("Pictures/Elseworld shoot/Shot list.txt", 2310, 96)
put("Pictures/Elseworld shoot/Elseworld shoot RAW.7z", 1_884_120_334, 144)
for i, (n, s) in enumerate((("01 Portal.flac", 31_224_118), ("02 Ruins of Elsewhere.flac", 44_903_551),
                            ("03 Static Bloom.mp3", 10_842_212), ("04 Signal Lost.mp3", 9_540_100),
                            ("05 Arc Light.mp3", 15_203_480))):
    put(f"Music/Paragon/ELSEWORLD/{n}", s, 9 * (i + 1))
(home / "Music" / "Paragon" / "ELSEWORLD (demos)").mkdir()
put("Documents/Elseworld mix notes.txt", 4480, 72)
put("Downloads/Elseworld master.wav", 412_220_480, 120)
put("Downloads/PR7-L-0.6.0-setup.exe", 48_220_114, 2)
put("Downloads/Invoice 2026-11.pdf", 182_448, 30)
put("Downloads/Live at the Arc.mp4", 912_448_221, 190)
photo(42).save(str(home / "Downloads" / "tour-poster.png"))
places.home = lambda: str(home)

from pr7e.main_window import MainWindow  # noqa: E402


def make_window(folders):
    app.setPalette(theme.palette())
    app.setStyleSheet(theme.stylesheet())
    w = MainWindow(folders)
    w.resize(1280, 800)
    w.answers = {"confirm": lambda p: True}
    w.show()
    return w


def settle(w, cond=lambda: True, timeout=5):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        app.processEvents()
        if cond() and not w.loader.pending:
            break
        time.sleep(0.02)
    app.processEvents()


def grab(w, name):
    settle(w)
    w.grab().save(str(out / name))
    print("wrote", out / name)


places.add_bookmark(str(home / "Music" / "Paragon" / "ELSEWORLD"))
places.add_bookmark(str(shoot))
places.set_starred([str(shoot / "Ridge at dusk.png"), str(home / "Music" / "Paragon" / "ELSEWORLD" /
                                                          "03 Static Bloom.mp3")], True)

# 1. Grid: a folder of photos (real thumbnails), two tabs, three selected.
w = make_window([str(shoot), str(home / "Downloads")])
w.set_current_tab(0)
w.set_view("grid")
settle(w, lambda: all(e.size >= 0 for e in w.tab.model.rows))
w.tab.select_paths([str(shoot / n) for n in ("DSC_0418.jpg", "DSC_0421.jpg", "DSC_0433.jpg")])
grab(w, "grid.png")

# 2. Search in Home, list view, with the What filter; a copy running with its popover open.
w.set_current_tab(1)
w.go(str(home))
w.set_view("list")
w.start_search("elseworld")
w._run_search()
settle(w, lambda: w.tab.search.get("done"))


class Fake(ops.Job):
    def __init__(self, title, frac, total):
        super().__init__()
        self.title, self.total_bytes, self.done_bytes = title, total, int(total * frac)
        self.state, self.started = "running", time.monotonic() - 18


w.runner.running = [Fake("Copying “ELSEWORLD” to “USB DRIVE (E:)”", 0.34, 1_200_000_000),
                    Fake("Copying “Elseworld shoot RAW.7z” to “Backup”", 0.71, 1_884_120_334)]
w._tick()
w._key("operations")
w.tab.select_paths([str(home / "Music" / "Paragon" / "ELSEWORLD")])
grab(w, "search.png")
w.runner.running = []
w._tick()

# 3. The right-click menu on an archive.
w.end_search()
w.close_tab(1)
w.set_view("grid")
w.go(str(home / "Downloads"))
w.go(str(shoot))
settle(w)
target = shoot / "Elseworld shoot RAW.7z"
w.tab.select_paths([str(target)])
idx = w.tab.model.index(w.tab.model.row_of(str(target)), 0)
rect = w.tab.grid.visualRect(idx)
pos = w.tab.grid.viewport().mapTo(w, rect.center())
menus = []
w.answers["context"] = menus.append
w._item_menu(w.tab.selected())
menu = menus[0]
menu.popup(w.mapToGlobal(pos))
settle(w)
img = w.grab()
mp = menu.grab()
x = min(pos.x(), w.width() - mp.width() - 10)
y = max(80, min(pos.y(), w.height() - mp.height() - 10))
p = QPainter(img)
p.fillRect(QRectF(x + 4, y + 6, mp.width(), mp.height()), QColor(0, 0, 0, 90))
p.drawPixmap(x, y, mp)
p.end()
img.save(str(out / "menu.png"))
print("wrote", out / "menu.png")
menu.close()
del w.answers["context"]

# 4. Trash.
for rel in ("Downloads/Live at the Arc.mp4", "Downloads/Invoice 2026-11.pdf"):
    trash.send(str(home / rel))
w.go("trash:")
w.set_view("list")
w.tab.select_paths([w.tab.model.rows[0].path])
grab(w, "trash.png")

# 5. The "Replace file?" question, and Properties.
from pr7e import dialogs  # noqa: E402

c = ops._conflict(str(home / "Downloads" / "tour-poster.png"), str(shoot / "Ridge at dusk.png"))
d = dialogs.ConflictDialog(c, w)
d.show()
settle(w)
d.grab().save(str(out / "replace.png"))
print("wrote", out / "replace.png")
d.close()
w.go(str(home / "Pictures"))
from pr7e import fs  # noqa: E402

pd = dialogs.PropertiesDialog([fs.entry_for(str(shoot))], w)
pd.show()
settle(w, lambda: pd.counted is not None)
pd._show_count()
settle(w)
pd.grab().save(str(out / "properties.png"))
print("wrote", out / "properties.png")
pd.done(0)

# 6. AURORA.
w.set_finish("aurora")
w.set_view("grid")
w.go(str(shoot))
w.tab.select_paths([str(shoot / "Ridge at dusk.png")])
grab(w, "grid_aurora.png")
w.set_finish("obsidian")
w.close()
