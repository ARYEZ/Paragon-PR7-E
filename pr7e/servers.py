"""Connect to Server, as GNOME Files does it (no Qt): a server's folders are locations like
"sftp://user@host:22/home/user", browsed like any folder. Connections use PR7-F's engine (remote.py: FTP with
explicit TLS if available, implicit FTPS, SFTP).

- A `Server` keeps one connection for listing and small actions (under a lock); copies, opens and deletes run as
  jobs, each on a connection of its own, so a long upload never holds up browsing.
- Passwords are remembered only when asked to, and only in Windows Credential Manager (keyring, service
  "Paragon PR7-E"); PR7E_MEMORY_KEYRING keeps them in memory instead (tests). Recent servers never hold passwords.
- Host keys go in known_hosts in PR7-E's data folder (%APPDATA%\\Paragon\\PR7-E; PR7E_DATA overrides).
"""

from __future__ import annotations

import os
import posixpath
import re
import shutil
import tempfile
import threading
from dataclasses import dataclass, field

from . import fs, ops, remote

SCHEME = re.compile(r"^(ftp|ftps|ftpes|sftp)://", re.I)
SERVICE = "Paragon PR7-E"


def is_remote(loc: str) -> bool:
    return bool(loc) and bool(SCHEME.match(loc))


def split(loc: str) -> tuple[str, str]:
    """"sftp://me@host:22/a/b" -> ("sftp://me@host:22", "/a/b")."""
    m = SCHEME.match(loc)
    rest = loc[m.end():]
    host, slash, path = rest.partition("/")
    return loc[:m.end()] + host, "/" + path if slash else "/"


def join(key: str, path: str) -> str:
    return key + (path if path.startswith("/") else "/" + path)


def key_for(site: remote.Site) -> str:
    scheme = "sftp" if site.protocol == "sftp" else {"implicit": "ftps", "explicit": "ftpes"}.get(site.encryption,
                                                                                                    "ftp")
    host = f"[{site.host}]" if ":" in site.host else site.host
    user = f"{site.user}@" if site.user and site.user != "anonymous" else ""
    return f"{scheme}://{user}{host}:{site.real_port}"


def parse(address: str) -> tuple[remote.Site, str]:
    """What's typed in Connect to Server -> (site, folder). "host" alone means FTP, as in Files' bar."""
    a = address.strip()
    if not SCHEME.match(a):
        a = "ftp://" + a
    key, path = split(a)
    site = remote.parse_quick(key)
    if "@" in key[SCHEME.match(key).end():]:
        site.logon = "normal"
    return site, path


def data_dir() -> str:
    d = os.environ.get("PR7E_DATA") or os.path.join(os.environ.get("APPDATA") or os.path.expanduser("~/.config"),
                                                    "Paragon", "PR7-E")
    os.makedirs(d, exist_ok=True)
    return d


def known_hosts() -> str:
    return os.path.join(data_dir(), "known_hosts")


# --------------------------------------------------------------------------- passwords and recent servers
_memory: dict[str, str] = {}


def _keyring():
    if os.environ.get("PR7E_MEMORY_KEYRING"):
        return None
    try:
        import keyring
        from keyring.backends import fail
        return None if isinstance(keyring.get_keyring(), fail.Keyring) else keyring
    except Exception:  # noqa: BLE001 - no keyring: passwords simply aren't remembered
        return None


def saved_password(key: str) -> str | None:
    if os.environ.get("PR7E_MEMORY_KEYRING"):
        return _memory.get(key)
    kr = _keyring()
    try:
        return kr.get_password(SERVICE, key) if kr else None
    except Exception:  # noqa: BLE001
        return None


def save_password(key: str, password: str | None) -> None:
    if os.environ.get("PR7E_MEMORY_KEYRING"):
        if password is None:
            _memory.pop(key, None)
        else:
            _memory[key] = password
        return
    kr = _keyring()
    if not kr:
        return
    try:
        if password is None:
            kr.delete_password(SERVICE, key)
        else:
            kr.set_password(SERVICE, key, password)
    except Exception:  # noqa: BLE001
        pass


def recent() -> list[str]:
    from . import settings
    return [s for s in settings.get_list("servers") if isinstance(s, str)]


def add_recent(key: str) -> None:
    from . import settings
    items = [key] + [s for s in recent() if s != key]
    settings.set("servers", items[:10])


# --------------------------------------------------------------------------- connections
@dataclass
class Server:
    key: str
    site: remote.Site
    session: object = None
    home: str = "/"
    lock: threading.RLock = field(default_factory=threading.RLock)

    @property
    def label(self) -> str:
        return self.site.host

    def new_session(self, asks: dict | None = None):
        """A connection of its own (jobs). The host key / certificate were accepted when the server was added."""
        return remote.open_session(self.site, host_key_check=(asks or {}).get("hostkey"), known_hosts=known_hosts(),
                                   cert_check=(asks or {}).get("cert"))

    def call(self, name: str, *args):
        """Run a session method, reconnecting once if the connection was lost."""
        with self.lock:
            for attempt in (0, 1):
                try:
                    if self.session is None or not self.session.connected:
                        self.session = self.new_session()
                    return getattr(self.session, name)(*args)
                except remote.RemoteError as e:
                    lost = "Connection lost" in str(e) or (self.session is not None and not self.session.connected)
                    if attempt or not lost:
                        raise
                    self.session = None

    def list(self, path: str) -> list[fs.Entry]:
        out = []
        for e in self.call("list", path):
            if e.name in (".", ".."):
                continue
            out.append(fs.Entry(e.name, join(self.key, remote.join(path, e.name)), e.is_dir,
                                -1 if e.is_dir else e.size, e.mtime, e.name.startswith("."), extra=e.perms))
        return out

    def close(self):
        with self.lock:
            if self.session is not None:
                try:
                    self.session.close()
                except Exception:  # noqa: BLE001
                    pass
                self.session = None


CONNECTED: dict[str, Server] = {}


# --------------------------------------------------------------------------- searching and counting on servers
def search(srv: Server, root: str, text: str, when: str = "", what: str = "", show_hidden: bool = False,
           cancelled=lambda: False, recursive: bool = True):
    """Files' search in a server folder and the folders in it, on a connection of its own; the same rules as on
    this computer (fs.search_filter). Yields entries with LOCATION relative to `root`."""
    ok = fs.search_filter(text, when, what)
    if ok is None:
        return
    sess = srv.new_session()
    try:
        stack = [root]
        while stack:
            if cancelled():
                return
            folder = stack.pop()
            try:
                listing = sess.list(folder)
            except remote.RemoteError:
                continue
            rel = posixpath.relpath(folder, root) if folder != root else ""
            for e in sorted(listing, key=lambda x: fs.natural_key(x.name)):
                if e.name in (".", "..") or (e.name.startswith(".") and not show_hidden):
                    continue
                path = remote.join(folder, e.name)
                if recursive and e.is_dir and not e.is_link:
                    stack.append(path)
                ent = fs.Entry(e.name, join(srv.key, path), e.is_dir, -1 if e.is_dir else e.size, e.mtime,
                               extra=e.perms)
                if ok(ent):
                    ent.location = rel or posixpath.basename(root.rstrip("/")) or srv.label
                    yield ent
    finally:
        sess.close()


def tree_size(srv: Server, paths: list[str], cancelled=lambda: False, progress=None) -> tuple[int, int, int]:
    """(bytes, files, folders) under server paths, for Properties (a connection of its own)."""
    total = files = folders = 0
    sess = srv.new_session()
    try:
        stack = []
        for p in paths:
            folder, name = posixpath.split(p.rstrip("/"))
            e = next((x for x in sess.list(folder or "/") if x.name == name), None)
            if e is None:
                continue
            if e.is_dir:
                stack.append(p)
                folders += 1
            else:
                files += 1
                total += e.size
        while stack and not cancelled():
            folder = stack.pop()
            try:
                listing = sess.list(folder)
            except remote.RemoteError:
                continue
            for e in listing:
                if e.name in (".", ".."):
                    continue
                if e.is_dir and not e.is_link:
                    folders += 1
                    stack.append(remote.join(folder, e.name))
                else:
                    files += 1
                    total += e.size
            if progress:
                progress(total, files, folders)
    finally:
        sess.close()
    return total, files, folders


def connect(site: remote.Site, hostkey=None, cert=None) -> Server:
    """Connect (on a worker thread: the questions come through hostkey / cert) and keep the server."""
    sess = remote.open_session(site, host_key_check=hostkey, known_hosts=known_hosts(), cert_check=cert)
    key = key_for(site)
    srv = Server(key, site, sess)
    try:
        srv.home = sess.pwd() or "/"
    except remote.RemoteError:
        srv.home = "/"
    old = CONNECTED.pop(key, None)
    if old is not None:
        old.close()
    CONNECTED[key] = srv
    return srv


def server_for(loc: str) -> Server | None:
    return CONNECTED.get(split(loc)[0]) if is_remote(loc) else None


def disconnect(key: str) -> None:
    srv = CONNECTED.pop(key, None)
    if srv is not None:
        srv.close()


# --------------------------------------------------------------------------- jobs on servers
def _remote_conflict(src_is_dir, src_size, src_mtime, tgt_is_dir, tgt_size, tgt_mtime, src, tgt) -> ops.Conflict:
    return ops.Conflict(src, tgt, src_is_dir, tgt_is_dir, 0 if src_is_dir else src_size, src_mtime,
                        0 if tgt_is_dir else tgt_size, tgt_mtime)


class TransferJob(ops.Job):
    """Download server items into a local folder, or upload local items into a server folder (copy or move), with
    Files' "Replace file?" question and the progress pie. Its own connection."""

    def __init__(self, sources: list[str], dest: str, move: bool = False):
        super().__init__()
        self.sources, self.dest, self.move = sources, dest, move
        self.upload = is_remote(dest)
        self.kind = "move" if move else "copy"
        verb = ("Moving" if move else "Copying")
        what = ops._quote(posixpath.basename(sources[0].rstrip("/")) if is_remote(sources[0]) else sources[0]) \
            if len(sources) == 1 else ops._files(len(sources))
        where = posixpath.basename(split(dest)[1].rstrip("/")) or server_for(dest).label if self.upload else dest
        self.title = f"{verb} {what} to {ops._quote(where) if self.upload else ops._quote(dest)}"
        self.created: list[str] = []
        self.sess = None

    def work(self):
        srv = server_for(self.dest if self.upload else self.sources[0])
        if srv is None:
            raise OSError(0, "The server isn’t connected any more.")
        self.state = "preparing"
        self.sess = srv.new_session()
        try:
            if self.upload:
                self.total_bytes, self.total_items, _ = fs.tree_size(self.sources, lambda: self.cancelled)
                self.state = "running"
                folder = split(self.dest)[1]
                self.changed.add(self.dest)  # the server folder shown refreshes
                for s in self.sources:
                    self.check()
                    self._up(os.path.abspath(s), folder, os.path.basename(os.path.normpath(s)))
                    if self.move:
                        ops._remove(s)
                        self.changed.add(os.path.dirname(os.path.abspath(s)))
            else:
                self.total_bytes = sum(self._remote_size(split(s)[1]) for s in self.sources)
                self.state = "running"
                os.makedirs(self.dest, exist_ok=True)
                for s in self.sources:
                    self.check()
                    path = split(s)[1]
                    made = self._down(path, self.dest, posixpath.basename(path.rstrip("/")))
                    if made:
                        self.created.append(made)
                    if self.move:
                        _delete_tree(self.sess, path, self)
                self.changed.add(self.dest)
                if self.created and not self.move:
                    self.undo = ops.TrashJob(self.created, label="Copy")
        finally:
            try:
                self.sess.close()
            except Exception:  # noqa: BLE001
                pass

    def _entry(self, folder: str, name: str):
        try:
            return next((e for e in self.sess.list(folder) if e.name == name), None)
        except remote.RemoteError:
            return None

    def _remote_size(self, path: str) -> int:
        folder, name = posixpath.split(path.rstrip("/"))
        e = self._entry(folder or "/", name)
        if e is None:
            return 0
        if not e.is_dir:
            return e.size
        return sum(self._remote_size(remote.join(path, c.name)) for c in self.sess.list(path)
                   if c.name not in (".", ".."))

    def _progress(self, n):
        self.done_bytes += n

    # ---------------------------------------------------------------- downloads
    def _down(self, path: str, folder: str, name: str) -> str | None:
        e = self._entry(posixpath.dirname(path.rstrip("/")) or "/", name)
        if e is None:
            raise OSError(0, "It isn’t on the server any more.", name)
        target = os.path.join(folder, name)
        merging = False
        if os.path.lexists(target):
            st = os.stat(target)
            action = self.decide(path, target, _remote_conflict(e.is_dir, e.size, e.mtime, os.path.isdir(target),
                                                                st.st_size, st.st_mtime, path, target))
            if action == "skip":
                self.done_bytes += self._remote_size(path)
                return None
            if isinstance(action, tuple):
                return self._down_as(path, folder, action[1], e)
            if action == "merge" and e.is_dir and os.path.isdir(target):
                merging = True
            else:
                ops._remove(target)
        return self._down_as(path, folder, name, e, merging)

    def _down_as(self, path, folder, name, e, merging=False):
        target = os.path.join(folder, name)
        if e.is_dir:
            if not merging:
                os.makedirs(target)
            for c in sorted(self.sess.list(path), key=lambda c: c.name.lower()):
                if c.name in (".", ".."):
                    continue
                self.check()
                try:
                    self._down(remote.join(path, c.name), target, c.name)
                except ops.Cancelled:
                    raise
                except (OSError, remote.RemoteError) as err:
                    self.errors.append(str(err))
            return None if merging else target
        try:
            self.sess.download(path, target, 0, self._progress, lambda: self.cancelled)
        except remote.RemoteError as err:
            if os.path.exists(target):
                os.remove(target)
            if self.cancelled:
                raise ops.Cancelled from err
            raise OSError(0, str(err), name) from err
        if e.mtime:
            try:
                os.utime(target, (e.mtime, e.mtime))
            except OSError:
                pass
        self.done_items += 1
        return target

    # ---------------------------------------------------------------- uploads
    def _up(self, local: str, folder: str, name: str) -> None:
        e = self._entry(folder, name)
        target = remote.join(folder, name)
        merging = False
        if e is not None:
            st = os.stat(local)
            action = self.decide(local, target, _remote_conflict(os.path.isdir(local), st.st_size, st.st_mtime,
                                                                 e.is_dir, e.size, e.mtime, local, target))
            if action == "skip":
                self.done_bytes += fs.tree_size([local])[0]
                return
            if isinstance(action, tuple):
                return self._up(local, folder, action[1])
            if action == "merge" and e.is_dir and os.path.isdir(local):
                merging = True
            else:
                _delete_tree(self.sess, target, self)
        if os.path.isdir(local):
            if not merging:
                self.sess.mkdir(target)
            for c in sorted(os.listdir(local), key=str.lower):
                self.check()
                try:
                    self._up(os.path.join(local, c), target, c)
                except ops.Cancelled:
                    raise
                except (OSError, remote.RemoteError) as err:
                    self.errors.append(str(err))
            return
        try:
            self.sess.upload(local, target, 0, self._progress, lambda: self.cancelled)
        except remote.RemoteError as err:
            if self.cancelled:
                try:
                    self.sess.delete(target)
                except remote.RemoteError:
                    pass
                raise ops.Cancelled from err
            raise OSError(0, str(err), name) from err
        self.done_items += 1


def _delete_tree(sess, path: str, job: ops.Job | None = None) -> None:
    folder, name = posixpath.split(path.rstrip("/"))
    e = next((x for x in sess.list(folder or "/") if x.name == name), None)
    if e is None:
        return
    if e.is_dir and not e.is_link:
        for c in sess.list(path):
            if c.name in (".", ".."):
                continue
            if job is not None:
                job.check()
            _delete_tree(sess, remote.join(path, c.name), job)
        sess.rmdir(path)
    else:
        sess.delete(path)


class DeleteJob(ops.Job):
    """Delete on a server, for good (servers have no Trash; Files asks first too)."""
    kind = "delete"

    def __init__(self, locs: list[str]):
        super().__init__()
        self.locs = locs
        name = posixpath.basename(split(locs[0])[1].rstrip("/"))
        self.title = f"Deleting {ops._quote(name) if len(locs) == 1 else ops._files(len(locs))}"
        self.total_items = len(locs)

    def work(self):
        srv = server_for(self.locs[0])
        if srv is None:
            raise OSError(0, "The server isn’t connected any more.")
        sess = srv.new_session()
        try:
            for loc in self.locs:
                self.check()
                try:
                    _delete_tree(sess, split(loc)[1], self)
                except remote.RemoteError as e:
                    self.errors.append(str(e))
                self.done_items += 1
                self.changed.add(posixpath.dirname(loc.rstrip("/")))
        finally:
            sess.close()


class RenameJob(ops.Job):
    kind = "rename"
    undo_label = "Rename"

    def __init__(self, loc: str, new_name: str):
        super().__init__()
        self.loc, self.new_name = loc, new_name
        key, path = split(loc)
        self.new_loc = join(key, remote.join(posixpath.dirname(path.rstrip("/")), new_name))
        self.title = f"Renaming {ops._quote(posixpath.basename(path))} as {ops._quote(new_name)}"
        self.total_items = 1

    def work(self):
        srv = server_for(self.loc)
        if srv is None:
            raise OSError(0, "The server isn’t connected any more.")
        try:
            srv.call("rename", split(self.loc)[1], split(self.new_loc)[1])
        except remote.RemoteError as e:
            raise OSError(0, str(e), self.loc) from e
        self.done_items = 1
        self.changed.add(posixpath.dirname(self.loc.rstrip("/")))
        self.undo = RenameJob(self.new_loc, posixpath.basename(split(self.loc)[1]))


class NewFolderJob(ops.Job):
    kind = "mkdir"
    undo_label = "Create Folder"

    def __init__(self, folder_loc: str, name: str):
        super().__init__()
        key, path = split(folder_loc)
        self.path = join(key, remote.join(path, name))
        self.title = f"Creating {ops._quote(name)}"
        self.total_items = 1

    def work(self):
        srv = server_for(self.path)
        if srv is None:
            raise OSError(0, "The server isn’t connected any more.")
        try:
            srv.call("mkdir", split(self.path)[1])
        except remote.RemoteError as e:
            raise OSError(0, str(e), self.path) from e
        self.done_items = 1
        self.changed.add(posixpath.dirname(self.path))
        self.undo = DeleteJob([self.path])
        self.undo.undo_label = "Create Folder"


class OpenJob(ops.Job):
    """Open a file on a server: it's downloaded to PR7-E's temporary folder and opened with its app."""
    kind = "open"

    def __init__(self, loc: str):
        super().__init__()
        self.loc = loc
        self.name = posixpath.basename(split(loc)[1])
        self.title = f"Opening {ops._quote(self.name)}"
        srv = server_for(loc)
        self.local = os.path.join(tempfile.gettempdir(), "PR7-E", srv.label if srv else "server", self.name)

    def work(self):
        srv = server_for(self.loc)
        if srv is None:
            raise OSError(0, "The server isn’t connected any more.")
        os.makedirs(os.path.dirname(self.local), exist_ok=True)
        sess = srv.new_session()
        try:
            folder, name = posixpath.split(split(self.loc)[1])
            e = next((x for x in sess.list(folder or "/") if x.name == name), None)
            self.total_bytes = e.size if e else 0
            part = self.local + ".pr7e-part"
            sess.download(split(self.loc)[1], part, 0, lambda n: setattr(self, "done_bytes", self.done_bytes + n),
                          lambda: self.cancelled)
            shutil.move(part, self.local)
        except remote.RemoteError as e:
            raise OSError(0, str(e), self.name) from e
        finally:
            sess.close()


class RelayJob(ops.Job):
    """Copy (or move) between two server folders, on one server or two: FTP and SFTP have no server-side copy, so
    the items come down to a temporary folder and go up again. Files' question is asked on the way up."""

    def __init__(self, sources: list[str], dest: str, move: bool = False):
        super().__init__()
        self.sources, self.dest, self.move = sources, dest, move
        self.kind = "move" if move else "copy"
        name = posixpath.basename(split(sources[0])[1].rstrip("/"))
        where = posixpath.basename(split(dest)[1].rstrip("/")) or (server_for(dest).label if server_for(dest)
                                                                   else dest)
        self.title = f"{'Moving' if move else 'Copying'} " + \
            (ops._quote(name) if len(sources) == 1 else ops._files(len(sources))) + f" to {ops._quote(where)}"
        self.steps: list[ops.Job] = []

    def fraction(self) -> float:
        if not self.steps:
            return 0.0
        return sum(s.fraction() for s in self.steps) / 2  # down, then up: each half the way

    def detail(self) -> str:
        step = self.steps[-1] if self.steps else None
        if step is None:
            return "Preparing\u2026"
        return ("Downloading: " if len(self.steps) == 1 else "Uploading: ") + step.detail()

    def cancel(self) -> None:
        super().cancel()
        for s in self.steps:
            s.cancel()

    def work(self):
        tmp = tempfile.mkdtemp(prefix="pr7e-relay-")
        try:
            down = TransferJob(self.sources, tmp)
            self.steps.append(down)
            down.run()
            self.errors += down.errors
            if down.state != "done" or self.cancelled:
                raise ops.Cancelled
            items = [os.path.join(tmp, n) for n in sorted(os.listdir(tmp))]
            up = TransferJob(items, self.dest)
            up.ask = self.ask
            self.steps.append(up)
            up.run()
            self.errors += up.errors
            if up.state != "done":
                raise ops.Cancelled
            self.changed.add(self.dest)
            if self.move and not self.errors:
                d = DeleteJob(self.sources)
                d.run()
                self.errors += d.errors
                self.changed |= d.changed
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
