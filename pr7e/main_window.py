"""PR7-E's window, laid out the way GNOME Files lays out its window: the sidebar of places on the left; on the
right the header bar (back, forward, the path bar, search, the view switch, the operations key, the main menu),
the tab bar (only with two or more tabs), and the folder in grid or list view, with the floating
"N items selected" label in its corner.

Mock-up stage: the views show the entries they're given; nothing touches the disk yet.
"""

from __future__ import annotations

import time
from dataclasses import dataclass, field

from PySide6.QtCore import QAbstractListModel, QAbstractTableModel, QModelIndex, QPointF, QRectF, QSize, Qt, Signal
from PySide6.QtGui import QColor, QFont, QFontMetrics, QPainter, QPainterPath, QPen, QPixmap
from PySide6.QtWidgets import (QAbstractItemView, QFrame, QHBoxLayout, QHeaderView, QLabel, QLineEdit, QListView,
                               QMainWindow, QMenu, QProgressBar, QStackedWidget, QStyle, QStyledItemDelegate,
                               QTreeView, QVBoxLayout, QWidget)

from . import theme
from .widgets import Header, SmallKey, draw_icon, file_pixmap, kind_of


@dataclass
class Entry:
    name: str
    is_dir: bool = False
    size: int = 0              # bytes; for folders, the number of items in them (Files shows "12 items")
    mtime: float = 0.0
    starred: bool = False
    thumb: QPixmap | None = None
    location: str = ""         # search results: the folder it's in, from where the search started
    kind: str = field(default="")

    def __post_init__(self):
        self.kind = self.kind or kind_of(self.name, self.is_dir)


def selected_text() -> QColor:
    """Text on the family selection band: white on OBSIDIAN, ink on AURORA (white on light pink can't be read)."""
    return QColor("#FFFFFF") if theme.FINISH == "obsidian" else QColor(theme.TEXT)


def human_size(n: int) -> str:
    """Files' way: decimal units, one decimal place (4.2 MB, 1.2 GB)."""
    if n < 1000:
        return f"{n} bytes" if n != 1 else "1 byte"
    for unit in ("kB", "MB", "GB", "TB"):
        n /= 1000
        if n < 1000 or unit == "TB":
            return f"{n:.1f} {unit}"
    return ""


def size_text(e: Entry) -> str:
    if e.is_dir:
        return f"{e.size} item" if e.size == 1 else f"{e.size} items"
    return human_size(e.size)


def date_text(t: float, now: float | None = None) -> str:
    """Files' Modified column: the time for today, "Yesterday", the weekday this week, else the date."""
    now = now or time.time()
    lt, ln = time.localtime(t), time.localtime(now)
    days = (time.mktime(ln[:3] + (0, 0, 0, 0, 0, -1)) - time.mktime(lt[:3] + (0, 0, 0, 0, 0, -1))) / 86400
    if days < 1:
        return time.strftime("%H:%M", lt)
    if days < 2:
        return "Yesterday"
    if days < 7:
        return time.strftime("%A", lt)
    return time.strftime("%d %b %Y", lt).lstrip("0")


# --------------------------------------------------------------------------- models (plain Python lists)
class EntryModel(QAbstractTableModel):
    """The folder's entries. A plain list model (never QStandardItemModel: PR7-Z's crash on clear())."""

    COLUMNS = ["NAME", "SIZE", "MODIFIED", ""]
    SEARCH_COLUMNS = ["NAME", "LOCATION", "SIZE", "MODIFIED", ""]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.rows: list[Entry] = []
        self.search = False

    def set_rows(self, rows: list[Entry], search: bool = False) -> None:
        self.beginResetModel()
        self.rows = list(rows)
        self.search = search
        self.endResetModel()

    def columns(self):
        return self.SEARCH_COLUMNS if self.search else self.COLUMNS

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def columnCount(self, parent=QModelIndex()):
        return len(self.columns())

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if orientation == Qt.Horizontal and role == Qt.DisplayRole:
            return self.columns()[section]
        return None

    def data(self, index, role=Qt.DisplayRole):
        e = self.rows[index.row()]
        col = self.columns()[index.column()]
        if role == Qt.DisplayRole:
            return {"NAME": e.name, "LOCATION": e.location, "SIZE": size_text(e), "MODIFIED": date_text(e.mtime),
                    "": ""}[col]
        if role == Qt.DecorationRole and col == "NAME":
            return file_pixmap(e.kind, 18)
        if role == Qt.TextAlignmentRole and col == "SIZE":
            return int(Qt.AlignRight | Qt.AlignVCenter)
        if role == Qt.ForegroundRole and col in ("SIZE", "MODIFIED", "LOCATION"):
            return QColor(theme.TEXT_DIM)
        if role == Qt.UserRole:
            return e
        return None


# --------------------------------------------------------------------------- grid view
class GridDelegate(QStyledItemDelegate):
    """Files' grid: a big icon or thumbnail, the name under it (two lines at most), a soft family band when
    selected."""

    def __init__(self, icon: int = 96, parent=None):
        super().__init__(parent)
        self.icon = icon

    def sizeHint(self, option, index):
        return QSize(self.icon + 44, self.icon + 54)

    def paint(self, p: QPainter, option, index):
        e: Entry = index.data(Qt.UserRole)
        r = QRectF(option.rect).adjusted(4, 4, -4, -4)
        selected = bool(option.state & QStyle.State_Selected)
        hover = bool(option.state & QStyle.State_MouseOver)
        p.save()
        p.setRenderHint(QPainter.Antialiasing)
        p.setRenderHint(QPainter.SmoothPixmapTransform)
        if selected or hover:
            p.setPen(QPen(theme.family_gradient(r.left(), 0, r.right(), 0), 1) if selected else Qt.NoPen)
            p.setBrush(theme.family_gradient(r.left(), 0, r.right(), 0, theme.LIT, 0.22 if selected else 0.07))
            p.drawRoundedRect(r.adjusted(.5, .5, -.5, -.5), 8, 8)
        box = QRectF(r.center().x() - self.icon / 2, r.top() + 6, self.icon, self.icon)
        if e.thumb is not None:
            pm = e.thumb
            k = min(box.width() / pm.width(), box.height() / pm.height())
            w, h = pm.width() * k, pm.height() * k
            t = QRectF(box.center().x() - w / 2, box.bottom() - h, w, h)  # thumbnails sit on the label
            clip = QPainterPath()
            clip.addRoundedRect(t, 4, 4)
            p.setClipPath(clip)
            p.drawPixmap(t, pm, QRectF(pm.rect()))
            p.setClipping(False)
            p.setPen(QPen(QColor(255, 255, 255, 40), 1))
            p.setBrush(Qt.NoBrush)
            p.drawRoundedRect(t.adjusted(.5, .5, -.5, -.5), 4, 4)
        else:
            p.drawPixmap(box.toRect(), file_pixmap(e.kind, self.icon))
        if e.starred:  # on the thumbnail's (or icon's) top-right corner
            corner = t if e.thumb is not None else box
            draw_icon(p, "star", QRectF(corner.right() - 13, corner.top() - 5, 18, 18), QColor(theme.BACKGROUND), 1.2,
                      QColor(theme.LED_ORANGE))
        f = QFont("Inter")
        f.setPixelSize(12)
        p.setFont(f)
        fm = QFontMetrics(f)
        text_rect = QRectF(r.left() + 6, box.bottom() + 6, r.width() - 12, 34)
        lines = _two_lines(e.name, fm, int(text_rect.width()))
        p.setPen(selected_text() if selected else QColor(theme.TEXT))
        p.drawText(text_rect, Qt.AlignHCenter | Qt.AlignTop, "\n".join(lines))
        p.restore()


def _two_lines(text: str, fm: QFontMetrics, width: int) -> list[str]:
    """Wrap at spaces into two lines; the second is elided in the middle, as Files does."""
    if fm.horizontalAdvance(text) <= width:
        return [text]
    words, first = text.split(" "), ""
    while words and fm.horizontalAdvance((first + " " + words[0]).strip()) <= width:
        first = (first + " " + words.pop(0)).strip()
    if not first:  # one long word
        return [fm.elidedText(text, Qt.ElideMiddle, width)]
    return [first, fm.elidedText(" ".join(words), Qt.ElideMiddle, width)]


class GridView(QListView):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setViewMode(QListView.IconMode)
        self.setResizeMode(QListView.Adjust)
        self.setMovement(QListView.Static)
        self.setUniformItemSizes(True)
        self.setSpacing(4)
        self.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.setMouseTracking(True)
        self.setItemDelegate(GridDelegate(96, self))
        self.setStyleSheet("QListView { border: none; padding: 10px; } "
                           "QListView::item:selected { background: transparent; }")


# --------------------------------------------------------------------------- list view
class ListDelegate(QStyledItemDelegate):
    """The star column: an outline star, filled orange when starred."""

    def paint(self, p, option, index):
        if index.model().columns()[index.column()] != "":
            return super().paint(p, option, index)
        e: Entry = index.data(Qt.UserRole)
        r = QRectF(option.rect)
        c = QColor(theme.LED_ORANGE) if e.starred else QColor(theme.TEXT_DIM)
        draw_icon(p, "star", QRectF(r.center().x() - 8, r.center().y() - 8, 16, 16), c, 1.3,
                  QColor(theme.LED_ORANGE) if e.starred else None)


class ListView(QTreeView):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setRootIsDecorated(False)
        self.setUniformRowHeights(True)
        self.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.setAlternatingRowColors(False)
        self.setIconSize(QSize(18, 18))
        self.setItemDelegate(ListDelegate(self))
        self.setStyleSheet("QTreeView { border: none; } QTreeView::item { height: 34px; }")
        self.header().setDefaultAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        self.header().setStretchLastSection(False)

    def setModel(self, model):
        super().setModel(model)
        model.modelReset.connect(self._columns)
        self._columns()

    def _columns(self):
        h = self.header()
        cols = self.model().columns()
        for i, c in enumerate(cols):
            h.setSectionResizeMode(i, QHeaderView.Stretch if c == "NAME" else QHeaderView.Fixed)
            self.setColumnWidth(i, {"LOCATION": 220, "SIZE": 100, "MODIFIED": 130, "": 44}.get(c, 300))

    def drawRow(self, painter, option, index):
        super().drawRow(painter, option, index)
        if self.selectionModel().isRowSelected(index.row(), index.parent()):
            r = QRectF(option.rect)
            r.setLeft(0)
            r.setRight(self.viewport().width())
            painter.save()
            painter.fillRect(r, theme.family_gradient(r.left(), 0, r.right(), 0, theme.LIT, 0.22))
            painter.setPen(QPen(theme.family_gradient(r.left(), 0, r.right(), 0), 1))
            painter.drawLine(QPointF(r.left(), r.top() + 0.5), QPointF(r.right(), r.top() + 0.5))
            painter.drawLine(QPointF(r.left(), r.bottom() - 0.5), QPointF(r.right(), r.bottom() - 0.5))
            painter.restore()


# --------------------------------------------------------------------------- sidebar
@dataclass
class Place:
    label: str
    icon: str                  # an icon name, or "" for the divider between groups
    path: str = ""
    eject: bool = False


class PlacesModel(QAbstractListModel):
    def __init__(self, places: list[Place], parent=None):
        super().__init__(parent)
        self.rows = places

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def data(self, index, role=Qt.DisplayRole):
        pl = self.rows[index.row()]
        if role == Qt.DisplayRole:
            return pl.label
        if role == Qt.UserRole:
            return pl
        return None

    def flags(self, index):
        if not self.rows[index.row()].icon:
            return Qt.NoItemFlags
        return Qt.ItemIsEnabled | Qt.ItemIsSelectable


class PlaceDelegate(QStyledItemDelegate):
    def sizeHint(self, option, index):
        return QSize(200, 13 if not index.data(Qt.UserRole).icon else 36)

    def paint(self, p, option, index):
        pl: Place = index.data(Qt.UserRole)
        r = QRectF(option.rect)
        p.save()
        p.setRenderHint(QPainter.Antialiasing)
        if not pl.icon:
            p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
            p.drawLine(QPointF(r.left() + 14, r.center().y()), QPointF(r.right() - 14, r.center().y()))
            p.restore()
            return
        band = r.adjusted(8, 2, -8, -2)
        selected = bool(option.state & QStyle.State_Selected)
        if selected:
            p.setPen(QPen(theme.family_gradient(band.left(), 0, band.right(), 0), 1))
            p.setBrush(theme.family_gradient(band.left(), 0, band.right(), 0, theme.LIT, 0.22))
            p.drawRoundedRect(band.adjusted(.5, .5, -.5, -.5), 6, 6)
        elif option.state & QStyle.State_MouseOver:
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(255, 255, 255, 14))
            p.drawRoundedRect(band, 6, 6)
        colour = selected_text() if selected else QColor(theme.TEXT)
        icon_colour = selected_text() if selected else QColor(theme.TEXT_DIM)
        draw_icon(p, pl.icon, QRectF(band.left() + 10, band.center().y() - 9, 18, 18), icon_colour, 1.5)
        f = QFont("Inter")
        f.setPixelSize(13)
        p.setFont(f)
        p.setPen(colour)
        p.drawText(QRectF(band.left() + 38, band.top(), band.width() - 70, band.height()),
                   Qt.AlignVCenter | Qt.AlignLeft, pl.label)
        if pl.eject:
            draw_icon(p, "eject", QRectF(band.right() - 28, band.center().y() - 8, 16, 16), QColor(theme.TEXT_DIM),
                      1.4)
        p.restore()


class Sidebar(QListView):
    def __init__(self, places: list[Place], parent=None):
        super().__init__(parent)
        self.setModel(PlacesModel(places, self))
        self.setItemDelegate(PlaceDelegate(self))
        self.setMouseTracking(True)
        self.setFixedWidth(232)
        self.setStyleSheet(f"QListView {{ background: {theme.TAB_FRAME}; border: none; "
                           f"border-right: 1px solid {theme.PANEL_EDGE}; border-radius: 0; padding: 8px 0; }} "
                           "QListView::item:selected { background: transparent; }")

    def select_path(self, path: str) -> None:
        for i, pl in enumerate(self.model().rows):
            if pl.path == path:
                self.setCurrentIndex(self.model().index(i))
                return


# --------------------------------------------------------------------------- the header bar
class PathBar(QWidget):
    """Files' path bar: the folders from the top of the place down to here, as buttons; the current one bright,
    and the folder menu (the vertical dots) at its end. Ctrl+L turns it into a text field."""

    clicked = Signal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.segments: list[tuple[str, str]] = []   # (icon or "", text)
        self.setFixedHeight(34)
        self.setMinimumWidth(200)

    def set_segments(self, segs: list[tuple[str, str]]) -> None:
        self.segments = segs
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = QRectF(self.rect()).adjusted(.5, .5, -.5, -.5)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.setBrush(QColor(theme.WIDGET))
        p.drawRoundedRect(r, 6, 6)
        f = QFont("Inter")
        f.setPixelSize(13)
        bold = QFont(f)
        bold.setWeight(QFont.DemiBold)
        x = r.left() + 12
        for i, (icon, text) in enumerate(self.segments):
            last = i == len(self.segments) - 1
            if i:
                p.setFont(f)
                p.setPen(QColor(theme.TEXT_DIM))
                p.drawText(QRectF(x, 0, 14, r.height()), Qt.AlignCenter, "/")
                x += 18
            if icon:
                draw_icon(p, icon, QRectF(x, r.center().y() - 8, 16, 16),
                          QColor(theme.TEXT if last else theme.TEXT_DIM), 1.5)
                x += 22
            font = bold if last else f
            p.setFont(font)
            p.setPen(QColor(theme.TEXT if last else theme.TEXT_DIM))
            w = QFontMetrics(font).horizontalAdvance(text)
            p.drawText(QRectF(x, 0, w + 2, r.height()), Qt.AlignVCenter | Qt.AlignLeft, text)
            x += w + 6
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.drawLine(QPointF(r.right() - 34, r.top() + 6), QPointF(r.right() - 34, r.bottom() - 6))
        draw_icon(p, "more", QRectF(r.right() - 26, r.center().y() - 9, 18, 18), QColor(theme.TEXT_DIM), 1.5)


class OperationsKey(SmallKey):
    """The header bar's progress key: a pie that fills as the running copies and moves go."""

    def __init__(self, parent=None):
        super().__init__("operations", "Show operations", True, parent)
        self.fraction = 0.0

    def paintEvent(self, e):
        super().paintEvent(e)
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        c = QRectF(self.width() / 2 - 7, self.height() / 2 - 9.5, 14, 14)
        path = QPainterPath()
        path.moveTo(c.center())
        path.arcTo(c, 90, -360 * self.fraction)
        path.closeSubpath()
        p.fillPath(path, theme.family_gradient(c.left(), 0, c.right(), 0))


class HeaderBar(QWidget):
    triggered = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(12, 8, 12, 8)
        lay.setSpacing(4)
        self.keys: dict[str, SmallKey] = {}
        for name, tip in (("back", "Back (Alt+Left)"), ("forward", "Forward (Alt+Right)")):
            self._key(lay, name, tip)
        lay.addSpacing(6)
        self.path = PathBar()
        self.search = QLineEdit()
        self.search.setPlaceholderText("Search everything in Home")
        self.search.setFixedHeight(34)
        self.search.hide()
        lay.addWidget(self.path, 1)
        lay.addWidget(self.search, 1)
        lay.addSpacing(6)
        self._key(lay, "search", "Search (Ctrl+F)", True)
        self._key(lay, "grid", "Grid view (Ctrl+1) / list view (Ctrl+2)")
        self.ops = OperationsKey()
        self.ops.hide()
        lay.addWidget(self.ops)
        self.keys["operations"] = self.ops
        self._key(lay, "menu", "Main menu (F10)")
        self.setStyleSheet(f"HeaderBar {{ background: {theme.BACKGROUND}; }}")

    def _key(self, lay, name, tip, checkable=False):
        k = SmallKey(name, tip, checkable)
        k.clicked.connect(lambda _=False, n=name: self.triggered.emit(n))
        self.keys[name] = k
        lay.addWidget(k)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.drawLine(0, self.height() - 1, self.width(), self.height() - 1)


# --------------------------------------------------------------------------- tabs
class TabStrip(QWidget):
    """Files' tab bar: tabs share the width, the title centred, a close key on the current one; shown only when
    two or more tabs are open."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.tabs: list[tuple[str, str]] = []    # (icon, title)
        self.current = 0
        self.setFixedHeight(38)

    def set_tabs(self, tabs, current=0):
        self.tabs, self.current = tabs, current
        self.setVisible(len(tabs) > 1)
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = QRectF(self.rect())
        p.fillRect(r, QColor(theme.TAB_FRAME))
        n = max(1, len(self.tabs))
        w = (r.width() - 16 - 4 * (n - 1)) / n
        f = QFont("Inter")
        f.setPixelSize(12.5)
        for i, (icon, title) in enumerate(self.tabs):
            t = QRectF(8 + i * (w + 4), 4, w, r.height() - 8)
            cur = i == self.current
            if cur:
                p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
                p.setBrush(QColor(theme.WIDGET))
                p.drawRoundedRect(t.adjusted(.5, .5, -.5, -.5), 6, 6)
                bar = QRectF(t.left() + 14, t.bottom() - 3, t.width() - 28, 2)
                p.setPen(Qt.NoPen)
                p.setBrush(theme.family_gradient(bar.left(), 0, bar.right(), 0))
                p.drawRoundedRect(bar, 1, 1)
                draw_icon(p, "close", QRectF(t.right() - 26, t.center().y() - 8, 16, 16), QColor(theme.TEXT_DIM), 1.4)
            p.setFont(f)
            fm = QFontMetrics(f)
            tw = fm.horizontalAdvance(title)
            x = t.center().x() - (tw + 22) / 2
            draw_icon(p, icon, QRectF(x, t.center().y() - 8, 16, 16), QColor(theme.TEXT if cur else theme.TEXT_DIM), 1.4)
            p.setPen(QColor(theme.TEXT if cur else theme.TEXT_DIM))
            p.drawText(QRectF(x + 22, t.top(), tw + 4, t.height()), Qt.AlignVCenter | Qt.AlignLeft, title)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.drawLine(0, int(r.height()) - 1, int(r.width()), int(r.height()) - 1)


# --------------------------------------------------------------------------- search filters, overlays
class Chip(QLabel):
    def __init__(self, field_name: str, value: str, parent=None):
        super().__init__(f"<span style='color:{theme.TEXT_DIM}'>{field_name}</span>&nbsp;&nbsp;{value}&nbsp;&nbsp;"
                         f"<span style='color:{theme.TEXT_DIM}'>✕</span>", parent)
        self.setStyleSheet(f"QLabel {{ background: {theme.WIDGET}; border: 1px solid {theme.ACCENT_DIM}; "
                           "border-radius: 13px; padding: 4px 12px; font-size: 11.5px; font-weight: 600; }")


class SearchFilters(QWidget):
    """Files' search filters: When (a date range) and What (a kind of file)."""

    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(14, 8, 14, 8)
        lay.setSpacing(8)
        self.lay = lay
        self.where = QLabel("")
        self.where.setProperty("role", "dim")
        self.setStyleSheet(f"SearchFilters {{ background: {theme.BACKGROUND}; }}")

    def set_filters(self, chips: list[tuple[str, str]], where: str) -> None:
        while self.lay.count():
            it = self.lay.takeAt(0)
            if it.widget() and it.widget() is not self.where:
                it.widget().deleteLater()
        for k, v in chips:
            self.lay.addWidget(Chip(k, v))
        self.lay.addStretch(1)
        self.where.setText(where)
        self.lay.addWidget(self.where)

    def paintEvent(self, _):
        p = QPainter(self)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.drawLine(0, self.height() - 1, self.width(), self.height() - 1)


class FloatingStatus(QLabel):
    """The "3 items selected (14.6 MB)" label Files floats in the view's bottom-right corner."""

    def __init__(self, parent):
        super().__init__(parent)
        self.setStyleSheet(f"QLabel {{ background: {theme.WIDGET}; color: {theme.TEXT}; border: 1px solid "
                           f"{theme.PANEL_EDGE}; border-radius: 8px; padding: 7px 14px; font-size: 12px; }}")
        self.hide()

    def show_text(self, text: str) -> None:
        self.setText(text)
        self.adjustSize()
        self.setVisible(bool(text))
        self.place()

    def place(self):
        par = self.parentWidget()
        self.move(par.width() - self.width() - 18, par.height() - self.height() - 16)
        self.raise_()


class OperationsPopover(QFrame):
    """What the operations key opens: each running copy or move, its progress, and a cancel key."""

    def __init__(self, parent):
        super().__init__(parent)
        self.setStyleSheet(f"OperationsPopover {{ background: {theme.MENU}; border: 1px solid {theme.PANEL_EDGE}; "
                           "border-radius: 10px; }")
        self.lay = QVBoxLayout(self)
        self.lay.setContentsMargins(16, 14, 16, 14)
        self.lay.setSpacing(12)
        self.setFixedWidth(380)
        self.hide()

    def set_operations(self, ops: list[tuple[str, str, float]]) -> None:
        for title, detail, frac in ops:
            row = QHBoxLayout()
            col = QVBoxLayout()
            col.setSpacing(5)
            t = QLabel(title)
            t.setStyleSheet(f"color: {theme.ON_DARK}; font-weight: 600; font-size: 12.5px;")
            t.setWordWrap(True)
            bar = QProgressBar()
            bar.setRange(0, 1000)
            bar.setValue(int(frac * 1000))
            bar.setTextVisible(False)
            bar.setFixedHeight(8)
            d = QLabel(detail)
            d.setStyleSheet(f"color: {theme.TEXT_DIM}; font-size: 11.5px;")
            col.addWidget(t)
            col.addWidget(bar)
            col.addWidget(d)
            row.addLayout(col, 1)
            row.addWidget(SmallKey("close", "Cancel"), 0, Qt.AlignVCenter)
            self.lay.addLayout(row)
        self.adjustSize()


# --------------------------------------------------------------------------- the window
class MainWindow(QMainWindow):
    def __init__(self, places: list[Place], parent=None):
        super().__init__(parent)
        self.setWindowTitle("PR7-E")
        self.resize(1280, 800)
        root = QWidget()
        v = QVBoxLayout(root)
        v.setContentsMargins(0, 0, 0, 0)
        v.setSpacing(0)
        v.addWidget(Header("FILES"))
        body = QHBoxLayout()
        body.setSpacing(0)
        self.sidebar = Sidebar(places)
        body.addWidget(self.sidebar)
        right = QVBoxLayout()
        right.setSpacing(0)
        self.headerbar = HeaderBar()
        right.addWidget(self.headerbar)
        self.tabs = TabStrip()
        self.tabs.hide()
        right.addWidget(self.tabs)
        self.filters = SearchFilters()
        self.filters.hide()
        right.addWidget(self.filters)
        self.view_area = QWidget()
        va = QVBoxLayout(self.view_area)
        va.setContentsMargins(0, 0, 0, 0)
        self.stack = QStackedWidget()
        va.addWidget(self.stack)
        self.model = EntryModel(self)
        self.grid = GridView()
        self.grid.setModel(self.model)
        self.list = ListView()
        self.list.setModel(self.model)
        self.stack.addWidget(self.grid)
        self.stack.addWidget(self.list)
        right.addWidget(self.view_area, 1)
        body.addLayout(right, 1)
        v.addLayout(body, 1)
        self.setCentralWidget(root)
        self.status = FloatingStatus(self.view_area)
        self.popover = OperationsPopover(self)
        self.headerbar.triggered.connect(self._key)
        for view in (self.grid, self.list):
            view.selectionModel().selectionChanged.connect(self._selection)

    # ----------------------------------------------------------------- showing things (the mock-up's inputs)
    def show_folder(self, segments, entries: list[Entry], sidebar_path: str = "") -> None:
        self.headerbar.path.set_segments(segments)
        self.headerbar.path.show()
        self.headerbar.search.hide()
        self.headerbar.keys["search"].setChecked(False)
        self.filters.hide()
        self.model.set_rows(entries)
        self.sidebar.select_path(sidebar_path)
        self._selection()

    def show_search(self, text: str, chips, where: str, entries: list[Entry]) -> None:
        self.headerbar.path.hide()
        self.headerbar.search.show()
        self.headerbar.search.setText(text)
        self.headerbar.keys["search"].setChecked(True)
        self.filters.set_filters(chips, where)
        self.filters.show()
        self.model.set_rows(entries, search=True)
        self._selection()

    def set_view(self, mode: str) -> None:
        self.stack.setCurrentWidget(self.grid if mode == "grid" else self.list)
        # The switch shows the view you'd go to, as in Files.
        self.headerbar.keys["grid"].icon_name = "list" if mode == "grid" else "grid"
        self.headerbar.keys["grid"].update()

    def select(self, names: set[str]) -> None:
        view = self.stack.currentWidget()
        sm = view.selectionModel()
        sm.clear()
        from PySide6.QtCore import QItemSelectionModel
        for i, e in enumerate(self.model.rows):
            if e.name in names:
                sm.select(self.model.index(i, 0), QItemSelectionModel.Select | QItemSelectionModel.Rows)

    def show_operations(self, ops, fraction: float, open_popover: bool = False) -> None:
        self.headerbar.ops.fraction = fraction
        self.headerbar.ops.show()
        self.popover.set_operations(ops)
        if open_popover:
            k = self.headerbar.ops
            pos = k.mapTo(self, k.rect().bottomRight())
            self.popover.move(pos.x() - self.popover.width() + 12, pos.y() + 6)
            self.popover.show()
            self.popover.raise_()

    def context_menu(self, entry: Entry) -> QMenu:
        """Files' right-click menu for a file, plus "Show more options" (the full Windows menu)."""
        m = QMenu(self)
        app = {"audio": "PR7-L", "text": "PR7-W", "archive": "PR7-Z"}.get(entry.kind, "Photos")
        m.addAction(f"Open with {app}\tReturn")
        m.addAction("Open with other application")
        m.addSeparator()
        for t in ("Cut\tCtrl+X", "Copy\tCtrl+C", "Move to…", "Copy to…", "Move to Trash\tDelete"):
            m.addAction(t)
        m.addSeparator()
        m.addAction("Rename…\tF2")
        m.addAction("Compress…")
        if entry.kind == "archive":
            m.addAction("Extract here")
            m.addAction("Extract to…")
        m.addSeparator()
        m.addAction("Unstar" if entry.starred else "Star")
        m.addAction("Properties\tCtrl+I")
        m.addSeparator()
        m.addAction("Show more options\tShift+F10")
        return m

    # ----------------------------------------------------------------- reactions
    def _key(self, name: str) -> None:
        if name == "grid":
            self.set_view("list" if self.stack.currentWidget() is self.grid else "grid")
        elif name == "operations":
            self.popover.setVisible(not self.popover.isVisible())

    def _selection(self, *_):
        view = self.stack.currentWidget()
        rows = sorted({i.row() for i in view.selectionModel().selectedIndexes()})
        if not rows:
            self.status.show_text("")
            return
        entries = [self.model.rows[r] for r in rows]
        if len(entries) == 1:
            e = entries[0]
            text = f"“{e.name}” selected ({size_text(e)})"
        else:
            files = [e for e in entries if not e.is_dir]
            dirs = len(entries) - len(files)
            parts = []
            if dirs:
                parts.append(f"{dirs} folder{'s' if dirs > 1 else ''} selected")
            if files:
                parts.append(f"{len(files)} item{'s' if len(files) > 1 else ''} selected "
                             f"({human_size(sum(e.size for e in files))})")
            text = ", ".join(parts)
        self.status.show_text(text)

    def resizeEvent(self, e):
        super().resizeEvent(e)
        self.status.place()
