"""Screenshots of the PR7-E mock-up (offscreen): python tools/screenshots.py OUT_DIR"""
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

from PySide6.QtCore import QPointF, QRectF, Qt  # noqa: E402
from PySide6.QtGui import QColor, QLinearGradient, QPainter, QPainterPath, QPixmap, QRadialGradient  # noqa: E402
from PySide6.QtWidgets import QApplication  # noqa: E402

from pr7e import theme  # noqa: E402

out = Path(sys.argv[1] if len(sys.argv) > 1 else "docs/mockup")
out.mkdir(parents=True, exist_ok=True)
app = QApplication([])
theme.load_fonts()


def photo(seed: int, w: int = 300, h: int = 200) -> QPixmap:
    """A stand-in photo in family colours: a dusk sky, a sun, layered hills."""
    rnd = random.Random(seed)
    pm = QPixmap(w, h)
    p = QPainter(pm)
    p.setRenderHint(QPainter.Antialiasing)
    sky = QLinearGradient(0, 0, 0, h)
    tops = ["#2A0A1E", "#1A0610", "#3B0B1A", "#120A10"]
    mids = ["#C2183F", "#E2502A", "#D83A6A", "#F07A2A"]
    sky.setColorAt(0, QColor(rnd.choice(tops)))
    sky.setColorAt(0.65, QColor(rnd.choice(mids)))
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
    return pm


def portrait(seed):
    return photo(seed, 200, 300)


def make_window():
    app.setPalette(theme.palette())
    app.setStyleSheet(theme.stylesheet())
    from pr7e import main_window
    from pr7e.main_window import Entry, MainWindow, Place
    places = [Place("Recent", "recent", "recent:"), Place("Starred", "star", "starred:"),
              Place("Home", "home", "~"), Place("Desktop", "desktop", "~/Desktop"),
              Place("Documents", "documents", "~/Documents"), Place("Downloads", "downloads", "~/Downloads"),
              Place("Music", "music", "~/Music"), Place("Pictures", "pictures", "~/Pictures"),
              Place("Videos", "videos", "~/Videos"), Place("Trash", "trash", "trash:"), Place("", ""),
              Place("ELSEWORLD", "bookmark", "~/Music/Paragon/ELSEWORLD"), Place("Projects", "bookmark", "~/Projects"),
              Place("Elseworld shoot", "bookmark", "~/Pictures/Elseworld shoot"), Place("", ""),
              Place("Windows (C:)", "drive", "C:\\"), Place("USB DRIVE (E:)", "usb", "E:\\", eject=True),
              Place("Other Locations", "network", "other:")]
    w = MainWindow(places)
    return w, main_window, Entry


now = time.time()
H, D = 3600, 86400


def shoot(Entry):
    rows = [Entry("Contact sheets", True, 6, now - 2 * D), Entry("Edits", True, 14, now - 3 * H)]
    for i, (name, kb) in enumerate((("DSC_0412.jpg", 6_212), ("DSC_0418.jpg", 5_904), ("DSC_0421.jpg", 6_480),
                                    ("DSC_0433.jpg", 4_118), ("DSC_0437.jpg", 5_377), ("DSC_0440.jpg", 6_002),
                                    ("DSC_0452.jpg", 5_590), ("DSC_0459.jpg", 4_871), ("Ridge at dusk.png", 18_440),
                                    ("Static Bloom cover.png", 9_202), ("DSC_0466.jpg", 5_133))):
        e = Entry(name, False, kb * 1000 + 377, now - (i + 1) * 5 * H, starred=name == "Ridge at dusk.png")
        e.thumb = portrait(i) if i in (3, 7) else photo(i)
        rows.append(e)
    rows.append(Entry("Shot list.txt", False, 2_310, now - 4 * D))
    rows.append(Entry("Elseworld shoot RAW.7z", False, 1_884_120_334, now - 6 * D))
    return rows


def grab(w, name):
    app.processEvents()
    w.grab().save(str(out / name))
    print("wrote", out / name)


# 1. The grid view: a folder of photos, two tabs, three selected.
w, mw, Entry = make_window()
w.show()
w.tabs.set_tabs([("pictures", "Elseworld shoot"), ("downloads", "Downloads")], 0)
w.show_folder([("home", "Home"), ("", "Pictures"), ("", "Elseworld shoot")], shoot(Entry), "~/Pictures/Elseworld shoot")
w.set_view("grid")
app.processEvents()
w.select({"DSC_0418.jpg", "DSC_0421.jpg", "DSC_0433.jpg"})
grab(w, "grid.png")

# 2. Search: type in a folder, list view, When / What filters; a copy running, its popover open.
music = [("01 Portal.flac", 31_224_118), ("02 Ruins of Elsewhere.flac", 44_903_551), ("03 Static Bloom.mp3", 10_842_212),
         ("04 Signal Lost.mp3", 9_540_100), ("05 Arc Light.mp3", 15_203_480)]
res = [Entry(n, False, s, now - (i + 1) * 9 * H, location="Music/Paragon/ELSEWORLD", starred=i == 2)
       for i, (n, s) in enumerate(music)]
res += [Entry("ELSEWORLD (demos)", True, 9, now - 2 * D, location="Music/Paragon"),
        Entry("Elseworld mix notes.txt", False, 4_480, now - 3 * D, location="Documents"),
        Entry("Elseworld master.wav", False, 412_220_480, now - 5 * D, location="Projects/ELSEWORLD/Bounce")]
w.set_view("list")
w.show_search("elseworld", [("WHEN", "Last 7 days"), ("WHAT", "Music")], "Searching Home and the folders in it", res)
w.sidebar.select_path("~")
w.show_operations([("Copying \u201cELSEWORLD\u201d to \u201cUSB DRIVE (E:)\u201d", "412.6 MB of 1.2 GB \u2014 42 seconds left",
                    0.34),
                   ("Moving 3 files to Trash", "2 of 3 files", 0.66)], 0.42, open_popover=True)
app.processEvents()
w.select({"02 Ruins of Elsewhere.flac"})
grab(w, "search.png")
w.close()

# 3. The right-click menu on an archive in Downloads (Files' menu + Show more options).
w, mw, Entry = make_window()
w.show()
dl = [Entry("PR7-L-0.6.0-setup.exe", False, 48_220_114, now - 2 * H),
      Entry("Elseworld shoot RAW.7z", False, 1_884_120_334, now - 6 * D),
      Entry("Invoice 2026-11.pdf", False, 182_448, now - 1.2 * D),
      Entry("Mixing notes.txt", False, 4_480, now - 3 * D),
      Entry("tour-poster.png", False, 2_412_993, now - 4 * D),
      Entry("Live at the Arc.mp4", False, 912_448_221, now - 8 * D),
      Entry("Static Bloom (radio edit).mp3", False, 8_120_007, now - 9 * D),
      Entry("Fonts", True, 23, now - 30 * D)]
dl[4].thumb = photo(42)
w.tabs.set_tabs([("pictures", "Elseworld shoot"), ("downloads", "Downloads")], 1)
w.show_folder([("home", "Home"), ("", "Downloads")], dl, "~/Downloads")
w.set_view("grid")
app.processEvents()
w.select({"Elseworld shoot RAW.7z"})
app.processEvents()
menu = w.context_menu(dl[1])
idx = w.model.index(1, 0)
rect = w.grid.visualRect(idx)
pos = w.grid.viewport().mapTo(w, rect.center())
menu.popup(w.mapToGlobal(pos))
app.processEvents()
img = w.grab()
p = QPainter(img)
mp = menu.grab()
p.fillRect(QRectF(pos.x() + 4, pos.y() + 6, mp.width(), mp.height()), QColor(0, 0, 0, 90))  # its shadow
p.drawPixmap(pos, mp)
p.end()
img.save(str(out / "menu.png"))
print("wrote", out / "menu.png")
menu.close()
w.close()

# 4. AURORA, the light finish: the same grid.
theme.set_finish("aurora")
w, mw, Entry = make_window()
w.show()
w.show_folder([("home", "Home"), ("", "Pictures"), ("", "Elseworld shoot")], shoot(Entry), "~/Pictures/Elseworld shoot")
w.set_view("grid")
app.processEvents()
w.select({"Ridge at dusk.png"})
grab(w, "grid_aurora.png")
