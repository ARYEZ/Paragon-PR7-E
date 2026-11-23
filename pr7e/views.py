"""The pieces of PR7-E's window, as GNOME Files has them: the file model and its grid and list views (drag and
drop with Explorer too), the sidebar, the header bar with its path bar, the tab bar, the search filters, the Trash
bar, the operations popover and the floating selection label. main_window.py puts them together."""

from __future__ import annotations

import os

from PySide6.QtCore import (QAbstractListModel, QAbstractTableModel, QMimeData, QModelIndex, QPointF, QRectF, QSize,
                            Qt, QUrl, Signal)
from PySide6.QtGui import QColor, QDrag, QFont, QFontMetrics, QPainter, QPainterPath, QPen
from PySide6.QtWidgets import (QAbstractItemView, QFrame, QHBoxLayout, QHeaderView, QLabel, QLineEdit, QListView,
                               QMenu, QProgressBar, QPushButton, QStyle, QStyledItemDelegate, QTreeView, QVBoxLayout,
                               QWidget)

from . import fs, places, theme
from .places import Place
from .widgets import SmallKey, draw_icon, file_pixmap

ENTRY = Qt.UserRole
THUMB = Qt.UserRole + 1


def local_paths(urls) -> list[str]:
    """Files from a drop or the clipboard, as Windows writes paths (Qt gives "C:/x/y": normpath makes "C:\\x\\y")."""
    return [os.path.normpath(u.toLocalFile()) for u in urls if u.isLocalFile()]


def selected_text() -> QColor:
    """Text on the family selection band: white on OBSIDIAN, ink on AURORA (white on light pink can't be read)."""
    return QColor("#FFFFFF") if theme.FINISH == "obsidian" else QColor(theme.TEXT)


# --------------------------------------------------------------------------- the model
class EntryModel(QAbstractTableModel):
    """A location's entries. A plain list model (never QStandardItemModel: PR7-Z's crash on clear())."""

    COLUMNS = {"folder": ["NAME", "SIZE", "MODIFIED", ""],
               "search": ["NAME", "LOCATION", "SIZE", "MODIFIED", ""],
               "recent": ["NAME", "LOCATION", "SIZE", "MODIFIED", ""],
               "starred": ["NAME", "LOCATION", "SIZE", "MODIFIED", ""],
               "trash": ["NAME", "ORIGINAL LOCATION", "SIZE", "DELETED"],
               "other": ["NAME", "SPACE"]}

    def __init__(self, loader, parent=None):
        super().__init__(parent)
        self.loader = loader
        self.rows: list[fs.Entry] = []
        self.mode = "folder"
        self.stars: set[str] = set()
        self.by_path: dict[str, int] = {}
        self.thumbs_ok = lambda e: True   # the window's "Show thumbnails" preference

    def set_rows(self, rows: list[fs.Entry], mode: str = "folder") -> None:
        self.beginResetModel()
        self.rows = list(rows)
        self.mode = mode
        self._index()
        self.endResetModel()

    def append(self, rows: list[fs.Entry]) -> None:
        if not rows:
            return
        n = len(self.rows)
        self.beginInsertRows(QModelIndex(), n, n + len(rows) - 1)
        self.rows.extend(rows)
        self._index()
        self.endInsertRows()

    def _index(self):
        self.by_path = {os.path.normcase(e.path): i for i, e in enumerate(self.rows)}

    def row_of(self, path: str) -> int:
        return self.by_path.get(os.path.normcase(path), -1)

    def changed(self, path: str) -> None:
        r = self.row_of(path)
        if r >= 0:
            self.dataChanged.emit(self.index(r, 0), self.index(r, self.columnCount() - 1))

    def set_count(self, path: str, n: int) -> None:
        r = self.row_of(path)
        if r >= 0 and self.rows[r].is_dir:
            self.rows[r].size = n
            self.changed(path)

    def columns(self) -> list[str]:
        return self.COLUMNS[self.mode]

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def columnCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.columns())

    def headerData(self, section, orientation, role=Qt.DisplayRole):
        if orientation == Qt.Horizontal and role == Qt.DisplayRole:
            return self.columns()[section]
        return None

    def starred(self, e: fs.Entry) -> bool:
        return os.path.normcase(e.path) in self.stars

    def data(self, index, role=Qt.DisplayRole):
        if not index.isValid() or index.row() >= len(self.rows):
            return None
        e = self.rows[index.row()]
        col = self.columns()[index.column()]
        if role == Qt.DisplayRole:
            if col == "NAME":
                return e.name
            if col in ("LOCATION", "ORIGINAL LOCATION"):
                return e.location
            if col in ("SIZE", "SPACE"):
                return fs.size_text(e)
            if col in ("MODIFIED", "DELETED"):
                return fs.date_text(e.mtime)
            return ""
        if role == Qt.DecorationRole and col == "NAME":
            return file_pixmap(e.kind, 18)
        if role == Qt.TextAlignmentRole and col == "SIZE":
            return int(Qt.AlignRight | Qt.AlignVCenter)
        if role == Qt.ForegroundRole and col != "NAME":
            return QColor(theme.TEXT_DIM)
        if role == Qt.ToolTipRole and col == "NAME":
            return e.path
        if role == ENTRY:
            return e
        if role == THUMB:
            return self.loader.thumbnail(e) if self.loader is not None and e.kind in ("image", "video", "document") \
                and e.trash is None and self.thumbs_ok(e) else None
        return None

    def flags(self, index):
        f = Qt.ItemIsEnabled | Qt.ItemIsSelectable
        if self.mode != "other" and index.isValid():
            f |= Qt.ItemIsDragEnabled
        return f

    def mimeTypes(self):
        return ["text/uri-list"]

    def mimeData(self, indexes):
        md = QMimeData()
        rows = sorted({i.row() for i in indexes})
        md.setUrls([QUrl.fromLocalFile(self.rows[r].path) for r in rows])
        return md


# --------------------------------------------------------------------------- drag and drop (both views)
class DropMixin:
    """Files' drag and drop: dropping on a folder puts things in it, elsewhere in the folder shown. Same drive
    moves, another drive copies; Ctrl forces a copy, Shift a move. Dragging out gives Explorer the files too."""

    # Subclasses declare: dropped = Signal(list, str, str): paths, target folder ("" = the folder shown),
    # "copy" | "move" | "" (Files' rule); typed = Signal(str): type-to-search.
    can_drop = True

    def _drop_setup(self):
        self.setDragEnabled(True)
        self.setAcceptDrops(True)
        self.setDropIndicatorShown(False)
        self.setDragDropMode(QAbstractItemView.DragDrop)
        self.setDefaultDropAction(Qt.MoveAction)

    def _target_at(self, pos) -> str:
        idx = self.indexAt(pos)
        if idx.isValid():
            e = idx.data(ENTRY)
            if e is not None and e.is_dir and e.trash is None:
                return e.path
        return ""

    def startDrag(self, actions):
        idx = self.selectionModel().selectedRows() if hasattr(self.selectionModel(), "selectedRows") else []
        if not idx:
            return
        md = self.model().mimeData(idx)
        drag = QDrag(self)
        drag.setMimeData(md)
        drag.exec(Qt.CopyAction | Qt.MoveAction | Qt.LinkAction, Qt.MoveAction)

    def dragEnterEvent(self, e):
        if e.mimeData().hasUrls() and self.can_drop:
            e.acceptProposedAction()
        else:
            e.ignore()

    def dragMoveEvent(self, e):
        if e.mimeData().hasUrls() and self.can_drop:
            e.acceptProposedAction()
        else:
            e.ignore()

    def dropEvent(self, e):
        paths = local_paths(e.mimeData().urls())
        if not paths or not self.can_drop:
            e.ignore()
            return
        mods = e.modifiers()
        action = "copy" if mods & Qt.ControlModifier else ("move" if mods & Qt.ShiftModifier else "")
        target = self._target_at(e.position().toPoint())
        if target and any(os.path.normcase(p) == os.path.normcase(target) for p in paths):
            e.ignore()
            return
        e.setDropAction(Qt.CopyAction)  # we do the work: the source mustn't delete anything
        e.accept()
        self.dropped.emit(paths, target, action)

    def keyPressEvent(self, e):
        text = e.text()
        if text and text.isprintable() and not text.isspace() and not (e.modifiers() & (Qt.ControlModifier |
                                                                                          Qt.AltModifier)):
            self.typed.emit(text)
            return
        super().keyPressEvent(e)


# --------------------------------------------------------------------------- grid view
class GridDelegate(QStyledItemDelegate):
    """Files' grid: a big icon or thumbnail, the name under it (two lines at most), a soft family band when
    selected, a star on starred items."""

    def __init__(self, icon: int = 96, parent=None):
        super().__init__(parent)
        self.icon = icon

    def sizeHint(self, option, index):
        return QSize(self.icon + 44, self.icon + 54)

    def paint(self, p: QPainter, option, index):
        e: fs.Entry = index.data(ENTRY)
        if e is None:
            return
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
        img = index.data(THUMB)
        corner = box
        if img is not None:
            k = min(box.width() / img.width(), box.height() / img.height())
            w, h = img.width() * k, img.height() * k
            t = QRectF(box.center().x() - w / 2, box.bottom() - h, w, h)  # thumbnails sit on the label
            clip = QPainterPath()
            clip.addRoundedRect(t, 4, 4)
            p.setClipPath(clip)
            p.drawImage(t, img)
            p.setClipping(False)
            p.setPen(QPen(QColor(255, 255, 255, 40), 1))
            p.setBrush(Qt.NoBrush)
            p.drawRoundedRect(t.adjusted(.5, .5, -.5, -.5), 4, 4)
            corner = t
        else:
            p.drawPixmap(box.toRect(), file_pixmap(e.kind, self.icon))
        model = index.model()
        if hasattr(model, "starred") and model.starred(e):
            draw_icon(p, "star", QRectF(corner.right() - 13, corner.top() - 5, 18, 18), QColor(theme.BACKGROUND), 1.2,
                      QColor(theme.LED_ORANGE))
        f = QFont("Inter")
        f.setPixelSize(12)
        p.setFont(f)
        fm = QFontMetrics(f)
        text_rect = QRectF(r.left() + 6, box.bottom() + 6, r.width() - 12, 34)
        lines = two_lines(e.name, fm, int(text_rect.width()))
        p.setPen(selected_text() if selected else QColor(theme.TEXT))
        p.drawText(text_rect, Qt.AlignHCenter | Qt.AlignTop, "\n".join(lines))
        p.restore()


def two_lines(text: str, fm: QFontMetrics, width: int) -> list[str]:
    """Wrap at spaces into two lines; the second is elided in the middle, as Files does."""
    if fm.horizontalAdvance(text) <= width:
        return [text]
    words, first = text.split(" "), ""
    while words and fm.horizontalAdvance((first + " " + words[0]).strip()) <= width:
        first = (first + " " + words.pop(0)).strip()
    if not first:  # one long word
        return [fm.elidedText(text, Qt.ElideMiddle, width)]
    return [first, fm.elidedText(" ".join(words), Qt.ElideMiddle, width)]


class GridView(DropMixin, QListView):
    dropped = Signal(list, str, str)
    typed = Signal(str)
    ZOOMS = [48, 64, 96, 128, 192]

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setViewMode(QListView.IconMode)
        self.setResizeMode(QListView.Adjust)
        self.setMovement(QListView.Static)
        self.setUniformItemSizes(True)
        self.setSpacing(4)
        self.setWordWrap(True)
        self.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.setSelectionRectVisible(True)
        self.setMouseTracking(True)
        self.setContextMenuPolicy(Qt.CustomContextMenu)
        self.delegate = GridDelegate(96, self)
        self.setItemDelegate(self.delegate)
        self.setStyleSheet("QListView { border: none; padding: 10px; } "
                           "QListView::item:selected { background: transparent; }")
        self._drop_setup()

    def set_icon_size(self, px: int) -> None:
        self.delegate.icon = px
        self.scheduleDelayedItemsLayout()
        self.viewport().update()

    def selectedRows(self):
        return self.selectionModel().selectedIndexes()


# --------------------------------------------------------------------------- list view
class ListDelegate(QStyledItemDelegate):
    """The star column: an outline star, filled orange when starred."""

    def paint(self, p, option, index):
        model = index.model()
        if model.columns()[index.column()] != "":
            return super().paint(p, option, index)
        e: fs.Entry = index.data(ENTRY)
        r = QRectF(option.rect)
        on = model.starred(e)
        c = QColor(theme.LED_ORANGE) if on else QColor(theme.TEXT_DIM)
        draw_icon(p, "star", QRectF(r.center().x() - 8, r.center().y() - 8, 16, 16), c, 1.3,
                  QColor(theme.LED_ORANGE) if on else None)


class ListView(DropMixin, QTreeView):
    dropped = Signal(list, str, str)
    typed = Signal(str)
    star_clicked = Signal(object)
    sort_clicked = Signal(str)
    WIDTHS = {"LOCATION": 240, "ORIGINAL LOCATION": 260, "SIZE": 100, "MODIFIED": 130, "DELETED": 130, "": 44,
              "SPACE": 260}

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setRootIsDecorated(False)
        self.setItemsExpandable(False)
        self.setUniformRowHeights(True)
        self.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self.setSelectionBehavior(QAbstractItemView.SelectRows)
        self.setIconSize(QSize(18, 18))
        self.setItemDelegate(ListDelegate(self))
        self.setContextMenuPolicy(Qt.CustomContextMenu)
        self.setStyleSheet("QTreeView { border: none; } QTreeView::item { height: 34px; }")
        h = self.header()
        h.setDefaultAlignment(Qt.AlignLeft | Qt.AlignVCenter)
        h.setStretchLastSection(False)
        h.setSectionsClickable(True)
        h.setSortIndicatorShown(False)
        h.sectionClicked.connect(self._header)
        self._drop_setup()

    def setModel(self, model):
        super().setModel(model)
        model.modelReset.connect(self._columns)
        self._columns()

    def _columns(self):
        h = self.header()
        for i, c in enumerate(self.model().columns()):
            h.setSectionResizeMode(i, QHeaderView.Stretch if c == "NAME" else QHeaderView.Interactive)
            if c != "NAME":
                self.setColumnWidth(i, self.WIDTHS.get(c, 120))

    def _header(self, section):
        col = self.model().columns()[section]
        key = {"NAME": "name", "SIZE": "size", "MODIFIED": "mtime", "DELETED": "mtime", "SPACE": "name"}.get(col)
        if key:
            self.sort_clicked.emit(key)

    def selectedRows(self):
        return self.selectionModel().selectedRows()

    def mousePressEvent(self, e):
        idx = self.indexAt(e.position().toPoint())
        if idx.isValid() and e.button() == Qt.LeftButton and self.model().columns()[idx.column()] == "" and \
                not e.modifiers():
            self.star_clicked.emit(idx.data(ENTRY))
            return
        super().mousePressEvent(e)

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
class PlacesModel(QAbstractListModel):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.rows: list[Place] = []

    def set_places(self, rows: list[Place]):
        self.beginResetModel()
        self.rows = rows
        self.endResetModel()

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def data(self, index, role=Qt.DisplayRole):
        pl = self.rows[index.row()]
        if role == Qt.DisplayRole:
            return pl.label
        if role == Qt.ToolTipRole and pl.icon:
            return places.SPECIAL.get(pl.path, pl.path)
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
        target = index.row() == getattr(self.parent(), "drop_row", -1)
        if selected or target:
            p.setPen(QPen(theme.family_gradient(band.left(), 0, band.right(), 0), 1))
            p.setBrush(theme.family_gradient(band.left(), 0, band.right(), 0, theme.LIT, 0.22 if selected else 0.1))
            p.drawRoundedRect(band.adjusted(.5, .5, -.5, -.5), 6, 6)
        elif option.state & QStyle.State_MouseOver:
            p.setPen(Qt.NoPen)
            p.setBrush(QColor(255, 255, 255, 14) if theme.FINISH == "obsidian" else QColor(0, 0, 0, 14))
            p.drawRoundedRect(band, 6, 6)
        colour = selected_text() if selected else QColor(theme.TEXT)
        icon_colour = selected_text() if selected else QColor(theme.TEXT_DIM)
        draw_icon(p, pl.icon, QRectF(band.left() + 10, band.center().y() - 9, 18, 18), icon_colour, 1.5)
        f = QFont("Inter")
        f.setPixelSize(13)
        p.setFont(f)
        p.setPen(colour)
        w = band.width() - (78 if pl.eject else 50)
        p.drawText(QRectF(band.left() + 38, band.top(), w, band.height()), Qt.AlignVCenter | Qt.AlignLeft,
                   QFontMetrics(f).elidedText(pl.label, Qt.ElideRight, int(w)))
        if pl.eject:
            draw_icon(p, "eject", self.eject_rect(option.rect), QColor(theme.TEXT_DIM), 1.4)
        p.restore()

    @staticmethod
    def eject_rect(rect) -> QRectF:
        band = QRectF(rect).adjusted(8, 2, -8, -2)
        return QRectF(band.right() - 28, band.center().y() - 8, 16, 16)


class Sidebar(QListView):
    """Files' sidebar: places, bookmarks, drives; drop files on a place to copy/move them there (on Trash: to
    Trash); the eject key ejects."""

    activated_place = Signal(object, bool)     # Place, new tab
    dropped = Signal(list, object, str)        # paths, Place, action
    eject = Signal(object)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setModel(PlacesModel(self))
        self.setItemDelegate(PlaceDelegate(self))
        self.setMouseTracking(True)
        self.setFixedWidth(232)
        self.setAcceptDrops(True)
        self.setContextMenuPolicy(Qt.CustomContextMenu)
        self.drop_row = -1
        self.restyle()

    def restyle(self):
        self.setStyleSheet(f"QListView {{ background: {theme.TAB_FRAME}; border: none; "
                           f"border-right: 1px solid {theme.PANEL_EDGE}; border-radius: 0; padding: 8px 0; }} "
                           "QListView::item:selected { background: transparent; }")

    def place_at(self, pos) -> Place | None:
        idx = self.indexAt(pos)
        return idx.data(Qt.UserRole) if idx.isValid() and idx.data(Qt.UserRole).icon else None

    def select_path(self, path: str) -> None:
        for i, pl in enumerate(self.model().rows):
            if pl.icon and pl.path and os.path.normcase(pl.path) == os.path.normcase(path):
                self.selectionModel().blockSignals(True)
                self.setCurrentIndex(self.model().index(i))
                self.selectionModel().blockSignals(False)
                self.viewport().update()
                return
        self.selectionModel().clearSelection()
        self.viewport().update()

    def mouseReleaseEvent(self, e):
        idx = self.indexAt(e.position().toPoint())
        if idx.isValid() and e.button() in (Qt.LeftButton, Qt.MiddleButton):
            pl = idx.data(Qt.UserRole)
            if pl.icon:
                if pl.eject and PlaceDelegate.eject_rect(self.visualRect(idx)).contains(e.position()):
                    self.eject.emit(pl)
                    return
                self.activated_place.emit(pl, e.button() == Qt.MiddleButton or bool(e.modifiers() &
                                                                                       Qt.ControlModifier))
        super().mouseReleaseEvent(e)

    def dragEnterEvent(self, e):
        e.acceptProposedAction() if e.mimeData().hasUrls() else e.ignore()

    def dragMoveEvent(self, e):
        pl = self.place_at(e.position().toPoint())
        ok = e.mimeData().hasUrls() and pl is not None and (pl.path == "trash:" or os.path.isdir(pl.path))
        row = self.indexAt(e.position().toPoint()).row() if ok else -1
        if row != self.drop_row:
            self.drop_row = row
            self.viewport().update()
        e.acceptProposedAction() if ok else e.ignore()

    def dragLeaveEvent(self, e):
        self.drop_row = -1
        self.viewport().update()

    def dropEvent(self, e):
        self.drop_row = -1
        self.viewport().update()
        pl = self.place_at(e.position().toPoint())
        paths = local_paths(e.mimeData().urls())
        if pl is None or not paths:
            e.ignore()
            return
        mods = e.modifiers()
        action = "copy" if mods & Qt.ControlModifier else ("move" if mods & Qt.ShiftModifier else "")
        e.setDropAction(Qt.CopyAction)
        e.accept()
        self.dropped.emit(paths, pl, action)


# --------------------------------------------------------------------------- the header bar
class PathBar(QWidget):
    """Files' path bar: the folders from the top of the place down to here, as buttons; the current one bright,
    the folder menu (the vertical dots) at its end. Clicking elsewhere on it, or Ctrl+L, lets you type a path."""

    go = Signal(str)
    edit = Signal()
    menu = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.segments: list[tuple[str, str, str]] = []   # (icon, text, location)
        self.rects: list[QRectF] = []
        self.setFixedHeight(34)
        self.setMinimumWidth(200)
        self.setCursor(Qt.PointingHandCursor)

    def set_segments(self, segs) -> None:
        self.segments = segs
        self.update()

    def _fonts(self):
        f = QFont("Inter")
        f.setPixelSize(13)
        bold = QFont(f)
        bold.setWeight(QFont.DemiBold)
        return f, bold

    def _layout(self) -> list[tuple[float, float]]:
        """x, width of each segment; when they don't fit, the first ones are dropped (Files scrolls them)."""
        f, bold = self._fonts()
        widths = []
        for i, (icon, text, _loc) in enumerate(self.segments):
            font = bold if i == len(self.segments) - 1 else f
            widths.append((22 if icon else 0) + QFontMetrics(font).horizontalAdvance(text) + 8)
        avail = self.width() - 60
        first = 0
        while first < len(widths) - 1 and sum(widths[first:]) + 18 * (len(widths) - first) > avail:
            first += 1
        out, x = [], 12.0
        for i, w in enumerate(widths):
            if i < first:
                out.append((-1, 0))
                continue
            if i > first or first:
                x += 18
            out.append((x, w))
            x += w
        return out

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = QRectF(self.rect()).adjusted(.5, .5, -.5, -.5)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.setBrush(QColor(theme.WIDGET))
        p.drawRoundedRect(r, 6, 6)
        f, bold = self._fonts()
        self.rects = []
        for i, ((icon, text, _loc), (x, w)) in enumerate(zip(self.segments, self._layout())):
            if x < 0:
                self.rects.append(QRectF())
                continue
            last = i == len(self.segments) - 1
            if x > 12:
                p.setFont(f)
                p.setPen(QColor(theme.TEXT_DIM))
                p.drawText(QRectF(x - 22, 0, 18, r.height()), Qt.AlignCenter, "/")  # centred in the gap
            self.rects.append(QRectF(x - 4, 3, w, r.height() - 6))
            tx = x
            if icon:
                draw_icon(p, icon, QRectF(x, r.center().y() - 8, 16, 16),
                          QColor(theme.TEXT if last else theme.TEXT_DIM), 1.5)
                tx += 22
            p.setFont(bold if last else f)
            p.setPen(QColor(theme.TEXT if last else theme.TEXT_DIM))
            p.drawText(QRectF(tx, 0, w, r.height()), Qt.AlignVCenter | Qt.AlignLeft, text)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.drawLine(QPointF(r.right() - 34, r.top() + 6), QPointF(r.right() - 34, r.bottom() - 6))
        draw_icon(p, "more", self.menu_rect(), QColor(theme.TEXT_DIM), 1.5)

    def menu_rect(self) -> QRectF:
        return QRectF(self.width() - 27, self.height() / 2 - 9, 18, 18)

    def mouseReleaseEvent(self, e):
        pos = e.position()
        if pos.x() > self.width() - 36:
            self.menu.emit()
            return
        for (_icon, _text, loc), rect in zip(self.segments, self.rects):
            if rect.contains(pos):
                self.go.emit(loc)
                return
        self.edit.emit()


class OperationsKey(SmallKey):
    """The header bar's progress key: a pie that fills as the running copies and moves go."""

    def __init__(self, parent=None):
        super().__init__("operations", "Show operations", False, parent)
        self.fraction = 0.0

    def paintEvent(self, e):
        super().paintEvent(e)
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        c = QRectF(self.width() / 2 - 7, self.height() / 2 - 9.5, 14, 14)
        path = QPainterPath()
        path.moveTo(c.center())
        path.arcTo(c, 90, -360 * max(0.02, self.fraction))
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
        self.location = QLineEdit()
        self.location.setFixedHeight(34)
        self.location.hide()
        self.search = QLineEdit()
        self.search.setFixedHeight(34)
        self.search.setClearButtonEnabled(True)
        self.search.hide()
        for w in (self.path, self.location, self.search):
            lay.addWidget(w, 1)
        lay.addSpacing(6)
        self._key(lay, "search", "Search (Ctrl+F)", True)
        self._key(lay, "view", "List view (Ctrl+2)")
        self.ops = OperationsKey()
        self.ops.clicked.connect(lambda: self.triggered.emit("operations"))
        self.ops.hide()
        lay.addWidget(self.ops)
        self.keys["operations"] = self.ops
        self._key(lay, "menu", "Main menu (F10)")

    def _key(self, lay, name, tip, checkable=False):
        k = SmallKey(name, tip, checkable)
        k.clicked.connect(lambda _=False, n=name: self.triggered.emit(n))
        self.keys[name] = k
        lay.addWidget(k)

    def show_mode(self, mode: str) -> None:
        """"path", "location" (typing a path) or "search"."""
        self.path.setVisible(mode == "path")
        self.location.setVisible(mode == "location")
        self.search.setVisible(mode == "search")
        self.keys["search"].setChecked(mode == "search")

    def paintEvent(self, _):
        p = QPainter(self)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.drawLine(0, self.height() - 1, self.width(), self.height() - 1)


# --------------------------------------------------------------------------- tabs
class TabStrip(QWidget):
    """Files' tab bar: tabs share the width, the title centred, a close key on the current and hovered tab;
    shown only with two or more tabs. Middle click closes; double-clicking the empty bar opens a tab."""

    current_changed = Signal(int)
    close_requested = Signal(int)
    new_requested = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.tabs: list[tuple[str, str]] = []    # (icon, title)
        self.current = 0
        self.hover = -1
        self.setFixedHeight(38)
        self.setMouseTracking(True)
        self.hide()

    def set_tabs(self, tabs, current=0):
        self.tabs, self.current = tabs, current
        self.setVisible(len(tabs) > 1)
        self.update()

    def _rects(self) -> list[QRectF]:
        n = max(1, len(self.tabs))
        w = min(360.0, (self.width() - 16 - 4 * (n - 1)) / n)
        return [QRectF(8 + i * (w + 4), 4, w, self.height() - 8) for i in range(len(self.tabs))]

    @staticmethod
    def _close(t: QRectF) -> QRectF:
        return QRectF(t.right() - 26, t.center().y() - 8, 16, 16)

    def mouseMoveEvent(self, e):
        h = next((i for i, t in enumerate(self._rects()) if t.contains(e.position())), -1)
        if h != self.hover:
            self.hover = h
            self.update()

    def leaveEvent(self, _):
        self.hover = -1
        self.update()

    def mouseReleaseEvent(self, e):
        for i, t in enumerate(self._rects()):
            if t.contains(e.position()):
                if e.button() == Qt.MiddleButton or (e.button() == Qt.LeftButton and
                                                      self._close(t).adjusted(-3, -3, 3, 3).contains(e.position())):
                    self.close_requested.emit(i)
                elif e.button() == Qt.LeftButton:
                    self.current_changed.emit(i)
                return

    def mouseDoubleClickEvent(self, e):
        if not any(t.contains(e.position()) for t in self._rects()):
            self.new_requested.emit()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        r = QRectF(self.rect())
        p.fillRect(r, QColor(theme.TAB_FRAME))
        f = QFont("Inter")
        f.setPixelSize(12.5)
        fm = QFontMetrics(f)
        for i, ((icon, title), t) in enumerate(zip(self.tabs, self._rects())):
            cur = i == self.current
            if cur:
                p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
                p.setBrush(QColor(theme.WIDGET))
                p.drawRoundedRect(t.adjusted(.5, .5, -.5, -.5), 6, 6)
                bar = QRectF(t.left() + 14, t.bottom() - 3, t.width() - 28, 2)
                p.setPen(Qt.NoPen)
                p.setBrush(theme.family_gradient(bar.left(), 0, bar.right(), 0))
                p.drawRoundedRect(bar, 1, 1)
            elif i == self.hover:
                p.setPen(Qt.NoPen)
                p.setBrush(QColor(255, 255, 255, 10) if theme.FINISH == "obsidian" else QColor(0, 0, 0, 12))
                p.drawRoundedRect(t, 6, 6)
            if cur or i == self.hover:
                draw_icon(p, "close", self._close(t), QColor(theme.TEXT_DIM), 1.4)
            p.setFont(f)
            title = fm.elidedText(title, Qt.ElideMiddle, int(t.width() - 70))
            tw = fm.horizontalAdvance(title)
            x = t.center().x() - (tw + 22) / 2
            draw_icon(p, icon, QRectF(x, t.center().y() - 8, 16, 16), QColor(theme.TEXT if cur else theme.TEXT_DIM),
                      1.4)
            p.setPen(QColor(theme.TEXT if cur else theme.TEXT_DIM))
            p.drawText(QRectF(x + 22, t.top(), tw + 4, t.height()), Qt.AlignVCenter | Qt.AlignLeft, title)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.drawLine(0, int(r.height()) - 1, int(r.width()), int(r.height()) - 1)


# --------------------------------------------------------------------------- search filters, Trash bar
class Chip(QPushButton):
    """A search filter: WHEN · Last 7 days, a menu of choices; lit when one is chosen."""

    chosen = Signal(str)

    def __init__(self, field_name: str, choices: list[str], any_text: str, parent=None):
        super().__init__(parent)
        self.field_name, self.any_text = field_name, any_text
        self.value = ""
        m = QMenu(self)
        for c in [any_text] + choices:
            m.addAction(c, lambda c=c: self.set_value("" if c == any_text else c, emit=True))
        self.setMenu(m)
        self.setCursor(Qt.PointingHandCursor)
        self.set_value("")

    def set_value(self, v: str, emit: bool = False):
        self.value = v
        self.setText(f"{self.field_name}  ·  {v or self.any_text}")
        self.restyle()
        if emit:
            self.chosen.emit(v)

    def restyle(self):
        edge = theme.ACCENT if self.value else theme.PANEL_EDGE
        self.setStyleSheet(f"QPushButton {{ background: {theme.WIDGET}; color: {theme.TEXT}; border: 1px solid "
                           f"{edge}; border-radius: 13px; padding: 4px 14px; font-size: 11.5px; font-weight: 600; }}"
                           "QPushButton::menu-indicator { width: 0; }")


class SearchFilters(QWidget):
    """Files' search filters: When (a date range) and What (a kind of file)."""

    changed = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(14, 8, 14, 8)
        lay.setSpacing(8)
        self.when = Chip("WHEN", list(fs.WHEN), "Any time")
        self.what = Chip("WHAT", list(fs.WHAT), "Anything")
        for c in (self.when, self.what):
            c.chosen.connect(lambda _v: self.changed.emit())
            lay.addWidget(c)
        lay.addStretch(1)
        self.where = QLabel("")
        self.where.setProperty("role", "dim")
        lay.addWidget(self.where)
        self.hide()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.drawLine(0, self.height() - 1, self.width(), self.height() - 1)


class TrashBar(QWidget):
    """Shown in Trash: what Trash is, Restore (the selected items) and Empty."""

    restore = Signal()
    empty = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(14, 8, 14, 8)
        t = QLabel("Items in Trash can be restored, or deleted permanently by emptying the Trash.")
        t.setProperty("role", "dim")
        lay.addWidget(t, 1)
        self.restore_btn = QPushButton("Restore")
        self.empty_btn = QPushButton("Empty…")
        self.restore_btn.clicked.connect(self.restore)
        self.empty_btn.clicked.connect(self.empty)
        lay.addWidget(self.restore_btn)
        lay.addWidget(self.empty_btn)
        self.hide()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setPen(QPen(QColor(theme.PANEL_EDGE), 1))
        p.drawLine(0, self.height() - 1, self.width(), self.height() - 1)


# --------------------------------------------------------------------------- overlays
class FloatingStatus(QLabel):
    """The "3 items selected (14.6 MB)" label Files floats in the view's bottom-right corner."""

    def __init__(self, parent):
        super().__init__(parent)
        self.restyle()
        self.setAttribute(Qt.WA_TransparentForMouseEvents)
        self.hide()

    def restyle(self):
        self.setStyleSheet(f"QLabel {{ background: {theme.WIDGET}; color: {theme.TEXT}; border: 1px solid "
                           f"{theme.PANEL_EDGE}; border-radius: 8px; padding: 7px 14px; font-size: 12px; }}")

    def show_text(self, text: str) -> None:
        self.setText(text)
        self.adjustSize()
        self.setVisible(bool(text))
        self.place()

    def place(self):
        par = self.parentWidget()
        self.move(par.width() - self.width() - 18, par.height() - self.height() - 16)
        self.raise_()


class EmptyState(QWidget):
    """A big dim icon and a line in the middle of the view: "Folder is Empty", "No Results Found"..."""

    def __init__(self, parent):
        super().__init__(parent)
        self.icon, self.title, self.text = "folder", "", ""
        self.setAttribute(Qt.WA_TransparentForMouseEvents)
        self.hide()

    def show_state(self, icon: str, title: str, text: str = ""):
        self.icon, self.title, self.text = icon, title, text
        self.setVisible(bool(title))
        self.setGeometry(self.parentWidget().rect())
        self.raise_()
        self.update()

    def paintEvent(self, _):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing)
        c = QRectF(self.rect()).center()
        col = QColor(theme.TEXT_DIM)
        col.setAlphaF(0.6)
        draw_icon(p, self.icon, QRectF(c.x() - 40, c.y() - 90, 80, 80), col, 2.2)
        f = QFont("Inter")
        f.setPixelSize(20)
        f.setWeight(QFont.DemiBold)
        p.setFont(f)
        p.setPen(QColor(theme.TEXT))
        p.drawText(QRectF(0, c.y() + 2, self.width(), 30), Qt.AlignCenter, self.title)
        if self.text:
            f.setPixelSize(13)
            f.setWeight(QFont.Normal)
            p.setFont(f)
            p.setPen(QColor(theme.TEXT_DIM))
            p.drawText(QRectF(40, c.y() + 36, self.width() - 80, 40), Qt.AlignHCenter | Qt.TextWordWrap, self.text)


class OperationsPopover(QFrame):
    """What the operations key opens: each running copy or move, its progress, and a cancel key."""

    def __init__(self, parent):
        super().__init__(parent)
        self.setObjectName("ops")
        self.setStyleSheet(f"QFrame#ops {{ background: {theme.MENU}; border: 1px solid {theme.PANEL_EDGE}; "
                           "border-radius: 10px; }")
        self.lay = QVBoxLayout(self)
        self.lay.setContentsMargins(16, 14, 16, 14)
        self.lay.setSpacing(12)
        self.setFixedWidth(400)
        self.jobs: list = []
        self.rows: list[tuple[QLabel, QProgressBar, QLabel]] = []
        self.hide()

    def set_jobs(self, jobs) -> None:
        if [id(j) for j in jobs] != [id(j) for j in self.jobs]:
            while self.lay.count():
                it = self.lay.takeAt(0)
                if it.layout():
                    while it.layout().count():
                        w = it.layout().takeAt(0)
                        if w.widget():
                            w.widget().deleteLater()
                        elif w.layout():
                            while w.layout().count():
                                ww = w.layout().takeAt(0)
                                if ww.widget():
                                    ww.widget().deleteLater()
            self.jobs, self.rows = list(jobs), []
            for job in jobs:
                row = QHBoxLayout()
                col = QVBoxLayout()
                col.setSpacing(5)
                t = QLabel(job.title)
                t.setStyleSheet(f"color: {theme.ON_DARK}; font-weight: 600; font-size: 12.5px;")
                t.setWordWrap(True)
                bar = QProgressBar()
                bar.setRange(0, 1000)
                bar.setTextVisible(False)
                bar.setFixedHeight(8)
                d = QLabel("")
                d.setStyleSheet(f"color: {theme.TEXT_DIM}; font-size: 11.5px;")
                col.addWidget(t)
                col.addWidget(bar)
                col.addWidget(d)
                row.addLayout(col, 1)
                k = SmallKey("close", "Cancel")
                k.clicked.connect(job.cancel)
                row.addWidget(k, 0, Qt.AlignVCenter)
                self.lay.addLayout(row)
                self.rows.append((t, bar, d))
        for job, (_t, bar, d) in zip(self.jobs, self.rows):
            bar.setValue(int(job.fraction() * 1000))
            d.setText(job.detail())
        self.adjustSize()
