"""Work that runs off the GUI thread, so the window never stalls: file operations (one thread per job, as Files
runs them side by side), thumbnails and folder item counts (a small pool), and search. Results come back to the
GUI thread through queued signals."""

from __future__ import annotations

import os
import threading
from collections import OrderedDict

from PySide6.QtCore import QObject, QRunnable, QSize, Qt, QThreadPool, Signal
from PySide6.QtGui import QImage, QImageReader

from . import fs


class Asker(QObject):
    """ask(kind, payload) from any thread; the GUI thread answers through `handler(kind, payload)`."""

    _asked = Signal(object)

    def __init__(self, handler):
        super().__init__()
        self.handler = handler
        self._asked.connect(self._answer, Qt.QueuedConnection)

    def _answer(self, box):
        kind, payload, holder, event = box
        try:
            holder.append(self.handler(kind, payload))
        finally:
            event.set()

    def ask(self, kind: str, payload):
        if threading.current_thread() is threading.main_thread():
            return self.handler(kind, payload)
        holder, event = [], threading.Event()
        self._asked.emit((kind, payload, holder, event))
        event.wait()
        return holder[0] if holder else None


class JobRunner(QObject):
    """Runs ops.Job objects, each on its own thread. `finished(job)` fires on the GUI thread."""

    finished = Signal(object)
    _done = Signal(object)

    def __init__(self, asker: Asker):
        super().__init__()
        self.asker = asker
        self.running: list = []
        self._done.connect(self._finish, Qt.QueuedConnection)

    def start(self, job, sync: bool = False) -> None:
        job.ask = lambda conflict: self.asker.ask("conflict", conflict)
        if hasattr(job, "ask_password"):
            job.ask_password = lambda name: self.asker.ask("password", name)
        self.running.append(job)
        if sync:  # tests: run here and now
            job.run()
            self._finish(job)
            return

        def go():
            try:
                job.run()
            finally:
                self._done.emit(job)
        threading.Thread(target=go, daemon=True).start()

    def _finish(self, job):
        if job in self.running:
            self.running.remove(job)
            self.finished.emit(job)

    def wait(self, timeout: float = 30) -> bool:
        """Tests: until every job has finished (signals are delivered meanwhile)."""
        import time
        from PySide6.QtWidgets import QApplication
        end = time.monotonic() + timeout
        while self.running and time.monotonic() < end:
            QApplication.processEvents()
            time.sleep(0.01)
        QApplication.processEvents()
        return not self.running


# --------------------------------------------------------------------------- thumbnails and counts
class _Signals(QObject):
    thumb = Signal(str, float, object)    # path, mtime, QImage (or None)
    count = Signal(str, int)


class _Thumb(QRunnable):
    def __init__(self, path, mtime, size, signals):
        super().__init__()
        self.path, self.mtime, self.size, self.signals = path, mtime, size, signals

    def run(self):
        img = None
        try:
            r = QImageReader(self.path)
            r.setAutoTransform(True)  # camera photos stand the right way up (EXIF orientation)
            s = r.size()
            if s.isValid() and s.width() > 0 and s.height() > 0:
                k = min(1.0, self.size / max(s.width(), s.height()))
                r.setScaledSize(QSize(max(1, int(s.width() * k)), max(1, int(s.height() * k))))
            i = r.read()
            img = None if i.isNull() else i
        except Exception:  # noqa: BLE001 - a broken picture just keeps its icon
            img = None
        self.signals.thumb.emit(self.path, self.mtime, img)


class _ShellThumb(QRunnable):
    """Windows' own thumbnail (videos, PDFs, Office files, HEIC... whatever has a thumbnail handler)."""

    def __init__(self, path, mtime, size, signals):
        super().__init__()
        self.path, self.mtime, self.size, self.signals = path, mtime, size, signals

    def run(self):
        img = None
        try:
            from . import winshell
            got = winshell.thumbnail(self.path, self.size)
            if got:
                w, h, data, alpha = got
                fmt = QImage.Format_ARGB32_Premultiplied if alpha else QImage.Format_RGB32
                img = QImage(data, w, h, w * 4, fmt).copy()  # copy: the bytes object goes away
        except Exception:  # noqa: BLE001 - no thumbnail: the file keeps its icon
            img = None
        self.signals.thumb.emit(self.path, self.mtime, img)


class _Count(QRunnable):
    def __init__(self, path, show_hidden, signals):
        super().__init__()
        self.path, self.show_hidden, self.signals = path, show_hidden, signals

    def run(self):
        self.signals.count.emit(self.path, fs.count_items(self.path, self.show_hidden))


class Loader(QObject):
    """Thumbnails (pictures with Qt's image readers: JPEG, PNG, GIF, WebP, BMP, TIFF, ICO; on Windows everything else
    with a thumbnail handler through the shell: videos, PDFs, Office files, HEIC...) and item counts for
    folders. Thumbnails are cached by path and modification time, so going back to a folder is instant."""

    thumb_ready = Signal(str)
    count_ready = Signal(str, int)
    MAX_BYTES = 80_000_000      # bigger pictures keep their icon (Qt decodes them whole)
    THUMB = 256                 # px, enough for the largest zoom
    SHELL_KINDS = {"video", "document", "image"}  # Windows makes these (images Qt can't read: HEIC, SVG...)

    def __init__(self):
        super().__init__()
        self.pool = QThreadPool(self)
        self.pool.setMaxThreadCount(max(2, min(4, os.cpu_count() or 2)))
        self.signals = _Signals()
        self.signals.thumb.connect(self._thumb, Qt.QueuedConnection)
        self.signals.count.connect(self.count_ready, Qt.QueuedConnection)
        self.cache: OrderedDict[tuple[str, float], QImage | None] = OrderedDict()
        from . import winshell
        self.shell = winshell.available  # Windows' thumbnails; elsewhere pictures only
        self.pending: set[tuple[str, float]] = set()

    def thumbnail(self, e: fs.Entry) -> QImage | None:
        """The cached thumbnail, or None (one is made in the background if the file can have one)."""
        key = (e.path, e.mtime)
        if key in self.cache:
            self.cache.move_to_end(key)
            return self.cache[key]
        if key in self.pending or e.is_dir:
            return None
        ext = e.name.rsplit(".", 1)[-1].lower() if "." in e.name else ""
        if e.kind == "image" and ext in fs.IMAGE_EXTS and e.size <= self.MAX_BYTES:
            self.pending.add(key)
            self.pool.start(_Thumb(e.path, e.mtime, self.THUMB, self.signals))
        elif self.shell and e.kind in self.SHELL_KINDS:
            self.pending.add(key)
            self.pool.start(_ShellThumb(e.path, e.mtime, self.THUMB, self.signals))
        return None

    def _thumb(self, path, mtime, img):
        self.pending.discard((path, mtime))
        self.cache[(path, mtime)] = img
        while len(self.cache) > 3000:
            self.cache.popitem(last=False)
        self.thumb_ready.emit(path)

    def count(self, path: str, show_hidden: bool) -> None:
        self.pool.start(_Count(path, show_hidden, self.signals))

    def clear_queue(self) -> None:
        """Leaving a folder: forget work not started yet."""
        self.pool.clear()
        self.pending.clear()


# --------------------------------------------------------------------------- search
class Search(QObject):
    """fs.search on a thread; results come back in batches."""

    found = Signal(int, object)     # generation, list of entries
    done = Signal(int)

    def __init__(self):
        super().__init__()
        self.generation = 0
        self._stop = threading.Event()

    def start(self, root, text, when, what, show_hidden, sync=False, recursive=True) -> int:
        self.stop()
        self.generation += 1
        gen, stop = self.generation, threading.Event()
        self._stop = stop

        def go():
            batch = []
            import time
            last = time.monotonic()
            for e in fs.search(root, text, when, what, show_hidden, stop.is_set, recursive=recursive):
                batch.append(e)
                if time.monotonic() - last > 0.15 or len(batch) >= 300:
                    self.found.emit(gen, batch)
                    batch, last = [], time.monotonic()
            if batch and not stop.is_set():
                self.found.emit(gen, batch)
            self.done.emit(gen)
        if sync:
            go()
        else:
            threading.Thread(target=go, daemon=True).start()
        return gen

    def stop(self):
        self._stop.set()
