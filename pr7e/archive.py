"""Compress and Extract, as GNOME Files does them (no Qt), as ops.Job subclasses so they get the progress pie,
cancel and Undo like copies.

- Compress: Files' four choices. ".zip" and ".tar.xz" are made by Python itself (zipfile, tarfile); ".7z" and
  "Encrypted .zip" (AES-256) by 7-Zip's engine: PR7-Z's (found through PR7-Z's install entry), else an installed
  7-Zip, else 7z/7zz on PATH. PR7E_7Z overrides.
- Extract: .tar.* with Python's tarfile (7-Zip would peel a .tar.xz one layer at a time); everything else with
  7-Zip when there is one (7z, rar, zip with AES, iso, cab...), else Python for .zip. Files' rule: an archive holding one item at its top extracts as that item; one holding several
  extracts into a folder named after the archive. Taken names get " (2)", so nothing is overwritten. A password is
  asked for when the archive needs one.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import zipfile

from . import fs, ops

WINDOWS = sys.platform == "win32"
PR7Z_UNINSTALL = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\{9914C6C8-01EC-476F-845D-98F814A0426E}_is1"

# Files' Compress choices: (label, extension, needs 7-Zip, encrypted).
FORMATS = [(".zip", ".zip", False, False), ("Encrypted .zip", ".zip", True, True),
           (".tar.xz", ".tar.xz", False, False), (".7z", ".7z", True, False)]
EXTRACTABLE = {".zip", ".7z", ".rar", ".tar", ".gz", ".tgz", ".xz", ".txz", ".bz2", ".tbz2", ".zst", ".iso",
               ".cab", ".arj", ".lzh", ".wim", ".001"}
TAR_EXTRACT = (".tar", ".tar.gz", ".tgz", ".tar.xz", ".txz", ".tar.bz2", ".tbz2")
PYTHON_EXTRACT = (".zip",) + TAR_EXTRACT


def find_7z() -> str | None:
    env = os.environ.get("PR7E_7Z")
    if env:
        return env if os.path.isfile(env) else None
    cands = []
    if WINDOWS:
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, PR7Z_UNINSTALL) as k:
                loc = winreg.QueryValueEx(k, "InstallLocation")[0]
            cands.append(os.path.join(loc, "_internal", "engine", "7z.exe"))
        except OSError:
            pass
        cands.append(os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "PR7-Z", "_internal", "engine",
                                  "7z.exe"))
        for base in (os.environ.get("ProgramFiles"), os.environ.get("ProgramW6432"),
                     os.environ.get("ProgramFiles(x86)")):
            if base:
                cands.append(os.path.join(base, "7-Zip", "7z.exe"))
    for c in cands:
        if os.path.isfile(c):
            return c
    for name in ("7zz", "7z", "7za"):
        found = shutil.which(name)
        if found:
            return found
    return None


def can_extract(name: str) -> bool:
    low = name.lower()
    return any(low.endswith(e) for e in EXTRACTABLE)


def archive_stem(name: str) -> str:
    """"Photos.tar.gz" -> "Photos"; "disc.part1.rar" stays "disc.part1" (as Files names the folder)."""
    stem, _ext = fs.split_ext(name)
    return stem


def default_name(paths: list[str]) -> str:
    """Files' suggestion: the item's own name for one item, "Archive" for several."""
    if len(paths) == 1:
        p = paths[0]
        return os.path.basename(p) if os.path.isdir(p) else archive_stem(os.path.basename(p))
    return "Archive"


# --------------------------------------------------------------------------- running 7-Zip
_PERCENT = re.compile(rb"(\d{1,3})%")


def _run_7z(job: ops.Job, args: list[str], cwd: str | None = None) -> tuple[int, str]:
    """Run 7-Zip with -bsp1 progress; job.done_bytes follows its percentage. Cancel kills it."""
    exe = find_7z()
    if not exe:
        raise OSError(0, "This needs 7-Zip’s engine: install PR7-Z (or 7-Zip).")
    flags = 0x08000000 if WINDOWS else 0  # CREATE_NO_WINDOW
    p = subprocess.Popen([exe, *args, "-bsp1", "-bso1", "-bse1", "-y"], stdin=subprocess.DEVNULL,
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=flags, cwd=cwd)
    out = bytearray()
    try:
        while True:
            chunk = p.stdout.read1(4096) if hasattr(p.stdout, "read1") else p.stdout.read(256)
            if not chunk:
                break
            out += chunk
            m = _PERCENT.findall(chunk)
            if m and job.total_bytes:
                job.done_bytes = int(job.total_bytes * min(100, int(m[-1])) / 100)
            if job.cancelled:
                p.kill()
                p.wait()
                raise ops.Cancelled
        return p.wait(), out.decode("utf-8", "replace")
    finally:
        if p.poll() is None:
            p.kill()


def _wrong_password(text: str) -> bool:
    t = text.lower()
    return "wrong password" in t or "can not open encrypted archive" in t or "cannot open encrypted archive" in t \
        or "data error in encrypted file" in t


# --------------------------------------------------------------------------- Compress
class CompressJob(ops.Job):
    kind = "compress"
    undo_label = "Compress"

    def __init__(self, paths: list[str], dest: str, name: str, fmt: str = ".zip", password: str = ""):
        """fmt: one of FORMATS' labels. `name` is without the extension."""
        super().__init__()
        self.paths = [os.path.abspath(p) for p in paths]
        self.fmt = next(f for f in FORMATS if f[0] == fmt)
        self.password = password
        self.target = os.path.join(os.path.abspath(dest), name + self.fmt[1])
        self.title = f"Compressing {ops._quote(self.paths[0]) if len(self.paths) == 1 else ops._files(len(paths))}" \
                     f" into {ops._quote(self.target)}"
        self.made = ""

    def work(self):
        if os.path.lexists(self.target):
            raise OSError(0, "A file with that name already exists.", self.target)
        self.state = "preparing"
        self.total_bytes, self.total_items, _ = fs.tree_size(self.paths, lambda: self.cancelled)
        self.total_bytes = max(1, self.total_bytes)
        self.state = "running"
        folder = os.path.dirname(self.target)
        part = os.path.join(folder, f".{os.path.basename(self.target)}.pr7e-part")
        try:
            label, ext, needs_7z, encrypted = self.fmt
            if needs_7z:
                args = ["a", "-t7z" if ext == ".7z" else "-tzip"]
                if encrypted or self.password:
                    args.append("-p" + self.password)
                    if ext == ".7z":
                        args.append("-mhe=on")  # names are hidden too
                    else:
                        args.append("-mem=AES256")
                base = os.path.dirname(self.paths[0])
                code, out = self._in(base, args + [part] + [os.path.relpath(p, base) for p in self.paths])
                if code not in (0, 1):
                    raise OSError(0, _last_error(out) or "7-Zip couldn’t make the archive.", self.target)
            elif ext == ".zip":
                self._zip(part)
            else:
                self._tar(part)
            self.check()
            os.replace(part, self.target)
        except BaseException:
            if os.path.exists(part):
                os.remove(part)
            raise
        self.made = self.target
        self.done_bytes = self.total_bytes
        self.changed.add(folder)
        self.undo = ops.TrashJob([self.target], label="Compress")

    def _in(self, cwd, args):
        return _run_7z(self, args, cwd=cwd)  # 7-Zip stores the items under their own names, run from their folder

    def _walk(self):
        """(path on disk, name in the archive) for everything, folders first."""
        for p in self.paths:
            base = os.path.dirname(p)
            if os.path.isdir(p):
                for dirpath, dirs, files in os.walk(p):
                    dirs.sort()
                    yield dirpath, os.path.relpath(dirpath, base).replace("\\", "/") + "/"
                    for f in sorted(files):
                        full = os.path.join(dirpath, f)
                        yield full, os.path.relpath(full, base).replace("\\", "/")
            else:
                yield p, os.path.basename(p)

    def _zip(self, part):
        with zipfile.ZipFile(part, "w", zipfile.ZIP_DEFLATED, allowZip64=True) as z:
            for path, arc in self._walk():
                self.check()
                if arc.endswith("/"):
                    z.writestr(zipfile.ZipInfo.from_file(path, arc), b"")
                    continue
                info = zipfile.ZipInfo.from_file(path, arc)
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(path, "rb") as fi, z.open(info, "w", force_zip64=True) as fo:
                    while True:
                        self.check()
                        buf = fi.read(ops.CHUNK)
                        if not buf:
                            break
                        fo.write(buf)
                        self.done_bytes += len(buf)
                self.done_items += 1

    def _tar(self, part):
        job = self

        class Counting:
            def __init__(self, fh):
                self.fh = fh

            def read(self, n=-1):
                job.check()
                b = self.fh.read(n)
                job.done_bytes += len(b)
                return b
        with tarfile.open(part, "w:xz") as t:
            for path, arc in self._walk():
                self.check()
                info = t.gettarinfo(path, arc.rstrip("/"))
                if info.isreg():
                    with open(path, "rb") as fh:
                        t.addfile(info, Counting(fh))
                    self.done_items += 1
                else:
                    t.addfile(info)


def _last_error(out: str) -> str:
    lines = [ln.strip() for ln in out.replace("\b", "").splitlines() if ln.strip()]
    for ln in reversed(lines):
        if ln.startswith(("ERROR:", "Error:")) or "error" in ln.lower():
            return ln.split(":", 1)[-1].strip() or ln
    return ""


# --------------------------------------------------------------------------- Extract
class ExtractJob(ops.Job):
    kind = "extract"
    undo_label = "Extract"

    def __init__(self, archive: str, dest: str | None = None):
        super().__init__()
        self.archive = os.path.abspath(archive)
        self.dest = os.path.abspath(dest or os.path.dirname(self.archive))
        self.title = f"Extracting {ops._quote(self.archive)}"
        self.ask_password = None     # set by the runner: ask_password(archive name) -> str | None
        self.created: list[str] = []
        self.password = ""

    def work(self):
        self.state = "preparing"
        self.total_bytes = max(1, os.path.getsize(self.archive))
        os.makedirs(self.dest, exist_ok=True)
        staging = tempfile.mkdtemp(prefix=".pr7e-extract-", dir=self.dest)
        try:
            self.state = "running"
            tar = self.archive.lower().endswith(TAR_EXTRACT)
            if find_7z() and not tar:  # 7-Zip would unpack a .tar.xz one layer at a time: tar is Python's
                self._with_7z(staging)
            elif self.archive.lower().endswith(PYTHON_EXTRACT):
                self._with_python(staging)
            else:
                raise OSError(0, "Extracting this kind of archive needs 7-Zip’s engine: install PR7-Z (or "
                                 "7-Zip).", self.archive)
            self.check()
            self._place(staging)
        finally:
            shutil.rmtree(staging, ignore_errors=True)
        self.done_bytes = self.total_bytes
        self.changed.add(self.dest)
        if self.created:
            self.undo = ops.TrashJob(self.created, label="Extract")

    def _password(self) -> str:
        pw = self.ask_password(os.path.basename(self.archive)) if self.ask_password else None
        if pw is None:
            self.cancel()
            raise ops.Cancelled
        return pw

    def _with_7z(self, staging):
        while True:
            for name in os.listdir(staging):  # a wrong password leaves broken files: start clean
                ops._remove(os.path.join(staging, name))
            code, out = _run_7z(self, ["x", self.archive, "-o" + staging, "-p" + self.password])
            if code in (0, 1) and not _wrong_password(out):
                return
            if _wrong_password(out):
                self.password = self._password()
                continue
            raise OSError(0, _last_error(out) or "7-Zip couldn’t extract the archive.", self.archive)

    def _with_python(self, staging):
        if self.archive.lower().endswith(".zip"):
            with zipfile.ZipFile(self.archive) as z:
                infos = z.infolist()
                total = sum(i.file_size for i in infos) or 1
                self.total_bytes = total
                if any(i.flag_bits & 0x1 for i in infos):
                    while True:
                        self.password = self._password()
                        try:
                            z.setpassword(self.password.encode("utf-8"))
                            first = next(i for i in infos if i.flag_bits & 0x1)
                            z.read(first)
                            break
                        except RuntimeError:
                            continue
                for i in infos:
                    self.check()
                    z.extract(i, staging)  # zipfile drops ".." and absolute parts
                    self.done_bytes += i.file_size
                    self.done_items += 1
        else:
            with tarfile.open(self.archive) as t:
                members = t.getmembers()
                self.total_bytes = sum(m.size for m in members) or 1
                for m in members:
                    self.check()
                    t.extract(m, staging, filter="data")  # no paths outside the folder, no device files
                    self.done_bytes += m.size

    def _place(self, staging):
        """Files' rule: one item at the top comes out as itself; several go into a folder named after the
        archive."""
        top = sorted(os.listdir(staging))
        if not top:
            return
        if len(top) == 1:
            name = fs.free_name(top[0], self.dest, os.path.isdir(os.path.join(staging, top[0])))
            target = os.path.join(self.dest, name)
            os.rename(os.path.join(staging, top[0]), target)
        else:
            name = fs.free_name(archive_stem(os.path.basename(self.archive)), self.dest, True)
            target = os.path.join(self.dest, name)
            os.mkdir(target)
            for n in top:
                os.rename(os.path.join(staging, n), os.path.join(target, n))
        self.created.append(target)
