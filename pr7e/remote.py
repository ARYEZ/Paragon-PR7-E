"""Connections to servers, the way FileZilla makes them. (No Qt here: the window and the queue's workers use it.)

    FTP      explicit TLS when the server offers it (FileZilla's default "Use explicit FTP over TLS if available"),
             or required, or implicit TLS (port 990), or plain; passive mode; binary transfers; MLSD listings, LIST
             (Unix and Windows/IIS formats) when the server has no MLSD
    SFTP     over SSH (paramiko): password or key file; the server's host key is checked against PR7-E's known
             hosts and, when it's new or changed, the user is asked (FileZilla's "Unknown host key")

Every session has the same methods (list, download, upload with resume, mkdir, rename, delete, rmdir, chmod) and
reports what it sends and receives to `log(kind, text)`: kind is Status, Command, Response or Error, FileZilla's
message log. Passwords are never logged.
"""

from __future__ import annotations

import ftplib
import os
import posixpath
import re
import socket
import ssl
import stat as statmod
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone

ENCRYPTIONS = {"auto": "Use explicit FTP over TLS if available", "explicit": "Require explicit FTP over TLS",
               "implicit": "Require implicit FTP over TLS", "plain": "Only use plain FTP (insecure)"}
BLOCK = 64 * 1024


class RemoteError(Exception):
    """Shown to the user as it is."""


class HostKeyRejected(RemoteError):
    pass


@dataclass
class Site:
    """What a connection needs: the Site Manager's General tab."""
    host: str
    user: str = ""
    password: str = ""
    port: int = 0                     # 0: the protocol's default (21, 990 implicit, 22)
    protocol: str = "ftp"             # "ftp" or "sftp"
    encryption: str = "auto"          # FTP: auto | explicit | implicit | plain
    logon: str = "normal"             # anonymous | normal | ask | key
    key_file: str = ""                # SFTP logon "key"
    remote_dir: str = ""
    local_dir: str = ""
    name: str = ""
    id: str = ""                      # the Site Manager site it came from ("" for quick connections)
    timeout: float = 20.0

    @property
    def real_port(self) -> int:
        if self.port:
            return self.port
        if self.protocol == "sftp":
            return 22
        return 990 if self.encryption == "implicit" else 21


def parse_quick(host: str, user: str = "", password: str = "", port: str = "") -> Site:
    """The quick-connect bar, as FileZilla reads it: sftp:// or port 22 means SFTP, ftps:// implicit TLS,
    ftp:// or nothing FTP with TLS if available; user:pass@host works in the host box too."""
    h = host.strip()
    protocol, encryption = "ftp", "auto"
    m = re.match(r"^(ftp|ftps|ftpes|sftp)://", h, re.I)
    if m:
        scheme = m.group(1).lower()
        h = h[m.end():]
        protocol = "sftp" if scheme == "sftp" else "ftp"
        encryption = {"ftps": "implicit", "ftpes": "explicit", "ftp": "auto"}.get(scheme, "auto")
    h = h.split("/", 1)[0]
    if "@" in h:
        cred, h = h.rsplit("@", 1)
        u, _, pw = cred.partition(":")
        user = user or u
        password = password or pw
    if re.match(r"^\[.*\](:\d+)?$", h):  # [IPv6]:port
        addr, _, p = h[1:].partition("]")
        h, port = addr, (p.lstrip(":") or port)
    elif h.count(":") == 1:
        h, p = h.split(":")
        port = port or p
    n = int(port) if str(port).strip().isdigit() else 0
    if n == 22 and not m:
        protocol = "sftp"
    if not h:
        raise RemoteError("Type the server's address in Host.")
    return Site(host=h, user=user or ("anonymous" if protocol == "ftp" else ""), password=password, port=n,
                protocol=protocol, encryption=encryption, logon="normal" if user else "anonymous")


@dataclass
class Entry:
    name: str
    size: int = 0
    mtime: float = 0.0                # seconds since 1970, UTC; 0 unknown
    is_dir: bool = False
    is_link: bool = False
    perms: str = ""                   # "drwxr-xr-x" or ""
    owner: str = ""

    @property
    def mode(self) -> int | None:
        """The permission bits from perms, for chmod's dialog."""
        p = self.perms[-9:] if len(self.perms) >= 9 else ""
        if not re.fullmatch(r"[r-][w-][xsStT-][r-][w-][xsStT-][r-][w-][xtT-]", p):
            return None
        return sum(1 << (8 - i) for i, c in enumerate(p) if c not in "-ST")


def _noop(kind: str, text: str) -> None:
    pass


# ------------------------------------------------------------------------------------------ FTP listings
MONTHS = {m: i for i, m in enumerate(["jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov",
                                      "dec"], 1)}


def parse_mlsd_time(v: str) -> float:
    try:
        return datetime.strptime(v[:14], "%Y%m%d%H%M%S").replace(tzinfo=timezone.utc).timestamp()
    except ValueError:
        return 0.0


def parse_list_line(line: str, now: datetime | None = None) -> Entry | None:
    """One line of a LIST answer: Unix ls -l, or Windows/IIS (DOS) style. None for lines that aren't files."""
    now = now or datetime.now(timezone.utc)
    m = re.match(r"^([\-dlbcps])([rwxsStTl\-]{9})[+.@]?\s+\d+\s+(\S+)\s+(\S+)\s+(\d+)\s+"
                 r"(\w{3})\s+(\d{1,2})\s+(\d{1,2}:\d{2}|\d{4})\s+(.+)$", line)
    if m:
        kind, perms, owner, group, size, mon, day, ty, name = m.groups()
        month = MONTHS.get(mon.lower(), 1)
        if ":" in ty:
            hh, mm = (int(x) for x in ty.split(":"))
            year = now.year
            when = datetime(year, month, int(day), hh, mm, tzinfo=timezone.utc)
            if when > now.replace(microsecond=0) + __import__("datetime").timedelta(days=1):
                when = when.replace(year=year - 1)  # "Nov 20 18:04" in January is last year's
        else:
            when = datetime(int(ty), month, int(day), tzinfo=timezone.utc)
        is_link = kind == "l"
        if is_link and " -> " in name:
            name = name.split(" -> ", 1)[0]
        return Entry(name, int(size), when.timestamp(), kind == "d", is_link, kind + perms, f"{owner} {group}")
    m = re.match(r"^(\d{2})-(\d{2})-(\d{2,4})\s+(\d{1,2}):(\d{2})(AM|PM)\s+(<DIR>|\d+)\s+(.+)$", line, re.I)
    if m:
        mo, d, y, hh, mm, ampm, size, name = m.groups()
        y = int(y) + (2000 if len(y) == 2 and int(y) < 70 else 1900 if len(y) == 2 else 0)
        hour = int(hh) % 12 + (12 if ampm.upper() == "PM" else 0)
        when = datetime(y, int(mo), int(d), hour, int(mm), tzinfo=timezone.utc)
        is_dir = size.upper() == "<DIR>"
        return Entry(name, 0 if is_dir else int(size), when.timestamp(), is_dir)
    return None


# ------------------------------------------------------------------------------------------ FTP
class _FTP_TLS(ftplib.FTP_TLS):
    """ftplib's FTP over TLS, with the TLS session reused on data connections: FileZilla Server, vsftpd and
    ProFTPD refuse data connections that don't resume the control connection's session."""

    def ntransfercmd(self, cmd, rest=None):
        conn, size = ftplib.FTP.ntransfercmd(self, cmd, rest)
        if self._prot_p:
            conn = self.context.wrap_socket(conn, server_hostname=self.host, session=self.sock.session)
        return conn, size


class _FTP_TLS_Implicit(_FTP_TLS):
    """Implicit TLS (ftps://, port 990): the connection is encrypted from its first byte."""

    def connect(self, host="", port=0, timeout=-999, source_address=None):
        if host:
            self.host = host
        if port:
            self.port = port
        if timeout != -999:
            self.timeout = timeout
        self.sock = socket.create_connection((self.host, self.port), self.timeout)
        self.af = self.sock.family
        self.sock = self.context.wrap_socket(self.sock, server_hostname=self.host)
        self.file = self.sock.makefile("r", encoding=self.encoding)
        self.welcome = self.getresp()
        return self.welcome


def _tls_context(verify: bool) -> ssl.SSLContext:
    ctx = ssl.create_default_context()
    if not verify:
        # FileZilla shows an unknown certificate and lets the user trust it; most FTP servers have self-signed
        # ones. PR7-E asks the user (see FTPSession.cert_check) instead of refusing outright.
        ctx.check_hostname = False
        ctx.verify_mode = ssl.CERT_NONE
    return ctx


class FTPSession:
    protocol = "ftp"

    def __init__(self, site: Site, log=None, cert_check=None):
        """cert_check(host, info) -> bool is asked about a certificate Windows doesn't trust (self-signed);
        None accepts it, as FileZilla does after "Always trust"."""
        self.site, self.log = site, log or _noop
        self.cert_check = cert_check
        self.ftp: ftplib.FTP | None = None
        self.encrypted = False
        self.features: set[str] = set()
        self.cwd_path = "/"

    # -------------------------------------------------------------------------- connecting
    def _logged(self, fn, *args):
        """Run an ftplib call, putting its commands and responses in the message log."""
        try:
            return fn(*args)
        except ftplib.error_perm as e:
            self.log("Error", str(e))
            raise RemoteError(str(e)) from e
        except (ftplib.error_temp, ftplib.error_reply, ftplib.error_proto) as e:
            self.log("Error", str(e))
            raise RemoteError(str(e)) from e
        except (OSError, EOFError) as e:
            self.log("Error", f"Connection lost: {e}")
            raise RemoteError(f"Connection lost: {e}") from e

    def connect(self) -> None:
        s = self.site
        self.log("Status", f"Connecting to {s.host}:{s.real_port}...")
        enc = s.encryption
        try:
            if enc == "plain":
                ftp = ftplib.FTP(timeout=s.timeout)
            elif enc == "implicit":
                ftp = _FTP_TLS_Implicit(context=_tls_context(False), timeout=s.timeout)
            else:
                ftp = _FTP_TLS(context=_tls_context(False), timeout=s.timeout)
            ftp.encoding = "utf-8"
            self._hook(ftp)
            welcome = ftp.connect(s.host, s.real_port)
        except (OSError, EOFError) as e:
            self.log("Error", f"Could not connect to server: {e}")
            raise RemoteError(f"Could not connect to {s.host}:{s.real_port}: {e}") from e
        except ftplib.Error as e:
            raise RemoteError(str(e)) from e
        self.ftp = ftp
        self.log("Status", "Connection established, waiting for welcome message...")  # the welcome: logged by getresp
        if enc == "implicit":
            self._trusted()
            self.encrypted = True
        elif enc in ("auto", "explicit"):
            try:
                ftp.voidcmd("AUTH TLS")
            except ftplib.Error as e:
                if enc == "explicit":
                    self.close()
                    raise RemoteError("The server doesn't offer FTP over TLS, and this site requires it.") from e
                self.log("Status", "Insecure server, it does not support FTP over TLS.")
            else:
                self.log("Status", "Initializing TLS...")
                try:
                    ftp.sock = ftp.context.wrap_socket(ftp.sock, server_hostname=s.host)
                    ftp.file = ftp.sock.makefile("r", encoding=ftp.encoding)
                except (ssl.SSLError, OSError) as e:
                    self.close()
                    raise RemoteError(f"TLS failed: {e}") from e
                self._trusted()
                self.encrypted = True
                self.log("Status", f"TLS connection established ({ftp.sock.version()}).")
        self._login()

    def _trusted(self):
        if self.cert_check is None:
            return
        der = self.ftp.sock.getpeercert(binary_form=True) or b""
        import hashlib
        fp = ":".join(f"{b:02x}" for b in hashlib.sha256(der).digest())
        if not self.cert_check(self.site.host, {"sha256": fp, "der": der}):
            self.close()
            raise HostKeyRejected("The server's certificate wasn't trusted.")

    def _hook(self, ftp):
        """Put ftplib's own traffic in the message log (passwords hidden)."""
        log = self.log
        orig_putcmd, orig_getresp = ftp.putcmd, ftp.getresp

        def putcmd(line):
            log("Command", "PASS ********" if line.upper().startswith("PASS ") else line)
            return orig_putcmd(line)

        def getresp():
            resp = orig_getresp()
            for part in resp.splitlines():
                log("Response", part)
            return resp
        ftp.putcmd, ftp.getresp = putcmd, getresp

    def _login(self):
        s, ftp = self.site, self.ftp
        user = s.user or "anonymous"
        password = s.password if s.logon != "anonymous" else "anonymous@example.com"
        try:
            ftp.login(user, password, secure=False) if isinstance(ftp, ftplib.FTP_TLS) else ftp.login(user, password)
        except ftplib.error_perm as e:
            self.close()
            raise RemoteError(f"Login refused: {e}") from e
        except (ftplib.Error, OSError, EOFError) as e:
            self.close()
            raise RemoteError(f"Login failed: {e or type(e).__name__}") from e
        if self.encrypted:
            self._logged(ftp.voidcmd, "PBSZ 0")
            self._logged(ftp.voidcmd, "PROT P")
            ftp._prot_p = True
        self.log("Status", "Logged in")
        try:
            feat = ftp.sendcmd("FEAT")
            self.features = {ln.strip().split(" ")[0].upper() for ln in feat.splitlines()[1:-1]}
        except ftplib.Error:
            self.features = set()
        if "UTF8" in self.features:
            try:
                ftp.sendcmd("OPTS UTF8 ON")
            except ftplib.Error:
                pass
        self._logged(ftp.voidcmd, "TYPE I")
        self.cwd_path = self.pwd()

    @property
    def connected(self) -> bool:
        return self.ftp is not None

    def close(self):
        if self.ftp is not None:
            try:
                self.ftp.quit()
            except Exception:  # noqa: BLE001 - closing anyway
                try:
                    self.ftp.close()
                except Exception:  # noqa: BLE001
                    pass
            self.ftp = None

    def keepalive(self):
        self._logged(self.ftp.voidcmd, "NOOP")

    # -------------------------------------------------------------------------- directories
    def pwd(self) -> str:
        return self._logged(self.ftp.pwd) or "/"

    def list(self, path: str) -> list[Entry]:
        self.log("Status", f'Retrieving directory listing of "{path}"...')
        entries: list[Entry] = []
        if self.features & {"MLSD", "MLST"}:  # MLST in FEAT means MLSD works too (RFC 3659)
            for name, facts in self._logged(lambda: list(self.ftp.mlsd(path, ["type", "size", "modify", "perm",
                                                                                "unix.mode"]))):
                kind = facts.get("type", "").lower()
                if kind in ("cdir", "pdir") or name in (".", ".."):
                    continue
                mode = facts.get("unix.mode")
                perms = ""
                if mode and mode.isdigit():
                    perms = ("d" if kind == "dir" else "-") + statmod.filemode(int(mode, 8))[1:]
                entries.append(Entry(name, int(facts.get("size", 0) or 0), parse_mlsd_time(facts.get("modify", "")),
                                     kind == "dir", kind.startswith("os.unix=slink"), perms))
        else:
            lines: list[str] = []
            self._logged(self.ftp.cwd, path)
            self._logged(self.ftp.retrlines, "LIST -a" if "LIST -A" in self.features else "LIST", lines.append)
            for line in lines:
                e = parse_list_line(line)
                if e and e.name not in (".", ".."):
                    entries.append(e)
        self.log("Status", f'Directory listing of "{path}" successful')
        return entries

    def mkdir(self, path: str):
        self._logged(self.ftp.mkd, path)

    def rmdir(self, path: str):
        self._logged(self.ftp.rmd, path)

    def delete(self, path: str):
        self._logged(self.ftp.delete, path)

    def rename(self, old: str, new: str):
        self._logged(self.ftp.rename, old, new)

    def chmod(self, path: str, mode: int):
        self._logged(self.ftp.sendcmd, f"SITE CHMOD {mode:03o} {path}")

    def size(self, path: str) -> int | None:
        try:
            return self.ftp.size(path)
        except ftplib.Error:
            return None

    # -------------------------------------------------------------------------- transfers
    def download(self, remote: str, local: str, offset: int = 0, progress=None, cancelled=None) -> None:
        """Into `local`, from byte `offset` (resume: REST) when given."""
        mode = "ab" if offset else "wb"
        self.log("Status", f'Starting download of {remote}' + (f" (resuming at {offset:,} bytes)" if offset else ""))
        with open(local, mode) as fh:
            def write(chunk):
                if cancelled and cancelled():
                    raise _Cancel()
                fh.write(chunk)
                if progress:
                    progress(len(chunk))
            try:
                self._logged(self.ftp.retrbinary, f"RETR {remote}", write, BLOCK, offset or None)
            except _Cancel:
                self._abort()
                raise RemoteError("Cancelled") from None
        self.log("Status", f"File transfer successful, transferred {os.path.getsize(local) - offset:,} bytes")

    def upload(self, local: str, remote: str, offset: int = 0, progress=None, cancelled=None) -> None:
        """From `local`; with `offset`, the rest of the file is appended (resume: APPE)."""
        self.log("Status", f'Starting upload of {local}' + (f" (resuming at {offset:,} bytes)" if offset else ""))
        with open(local, "rb") as fh:
            fh.seek(offset)

            def cb(chunk):
                if cancelled and cancelled():
                    raise _Cancel()
                if progress:
                    progress(len(chunk))
            try:
                self._logged(self.ftp.storbinary, f"{'APPE' if offset else 'STOR'} {remote}", fh, BLOCK, cb)
            except _Cancel:
                self._abort()
                raise RemoteError("Cancelled") from None
        self.log("Status", "File transfer successful")

    def _abort(self):
        try:
            self.ftp.abort()
        except Exception:  # noqa: BLE001 - after a cancel the connection may be gone; the next use reconnects
            self.close()


class _Cancel(Exception):
    pass


# ------------------------------------------------------------------------------------------ SFTP
class SFTPSession:
    protocol = "sftp"

    def __init__(self, site: Site, log=None, host_key_check=None, known_hosts: str = ""):
        """host_key_check(host, port, key_type, fingerprint, changed) -> "once" | "always" | None is asked when
        the server's key isn't in `known_hosts` (or changed); None refuses, as FileZilla's Cancel."""
        self.site, self.log = site, log or _noop
        self.host_key_check = host_key_check
        self.known_hosts = known_hosts
        self.transport = None
        self.sftp = None
        self.encrypted = True
        self.cwd_path = "/"

    def connect(self) -> None:
        import paramiko
        s = self.site
        self.log("Status", f"Connecting to {s.host}:{s.real_port}...")
        try:
            sock = socket.create_connection((s.host, s.real_port), s.timeout)
            t = paramiko.Transport(sock)
            t.banner_timeout = s.timeout
            t.start_client(timeout=s.timeout)
        except (OSError, paramiko.SSHException, EOFError) as e:
            self.log("Error", f"Could not connect to server: {e}")
            raise RemoteError(f"Could not connect to {s.host}:{s.real_port}: {e}") from e
        self.transport = t
        key = t.get_remote_server_key()
        self._check_key(key)
        self.log("Status", f"Using username \"{s.user}\".")
        try:
            if s.logon == "key" and s.key_file:
                pkey = _load_key(s.key_file, s.password)
                t.auth_publickey(s.user, pkey)
            else:
                t.auth_password(s.user, s.password)
        except paramiko.AuthenticationException as e:
            self.close()
            raise RemoteError("Authentication failed: wrong user name, password or key.") from e
        except (paramiko.SSHException, OSError) as e:
            self.close()
            raise RemoteError(f"Authentication failed: {e}") from e
        self.sftp = paramiko.SFTPClient.from_transport(t)
        self.log("Status", f"Connected to {s.host}")
        self.cwd_path = self.pwd()

    def _check_key(self, key):
        import base64
        import hashlib
        s = self.site
        fp = "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
        name = s.host if s.real_port == 22 else f"[{s.host}]:{s.real_port}"
        known = _read_known(self.known_hosts)
        stored = known.get((name, key.get_name()))
        if stored == key.get_base64():
            return
        changed = any(h == name for h, _ in known)
        answer = self.host_key_check(s.host, s.real_port, key.get_name(), fp, changed) if self.host_key_check else None
        if answer not in ("once", "always"):
            self.close()
            raise HostKeyRejected("The server's host key wasn't trusted.")
        if answer == "always" and self.known_hosts:
            known = {k: v for k, v in known.items() if k[0] != name}
            known[(name, key.get_name())] = key.get_base64()
            _write_known(self.known_hosts, known)

    @property
    def connected(self) -> bool:
        return self.sftp is not None

    def close(self):
        for x in (self.sftp, self.transport):
            try:
                if x is not None:
                    x.close()
            except Exception:  # noqa: BLE001
                pass
        self.sftp = self.transport = None

    def keepalive(self):
        self.sftp.stat(".")

    def _call(self, fn, *args):
        try:
            return fn(*args)
        except OSError as e:
            text = e.strerror or str(e)
            self.log("Error", text)
            raise RemoteError(text) from e
        except Exception as e:  # noqa: BLE001 - paramiko's own errors, shown in words
            self.log("Error", str(e))
            raise RemoteError(str(e)) from e

    def pwd(self) -> str:
        return self._call(self.sftp.normalize, ".")

    def list(self, path: str) -> list[Entry]:
        self.log("Status", f'Listing directory {path}')
        out = []
        for a in self._call(self.sftp.listdir_attr, path):
            mode = a.st_mode or 0
            is_link = statmod.S_ISLNK(mode)
            is_dir = statmod.S_ISDIR(mode)
            if is_link:
                try:
                    is_dir = statmod.S_ISDIR(self.sftp.stat(posixpath.join(path, a.filename)).st_mode)
                except OSError:
                    pass
            owner = ""
            if getattr(a, "longname", None):
                parts = a.longname.split()
                if len(parts) > 3:
                    owner = f"{parts[2]} {parts[3]}"
            out.append(Entry(a.filename, a.st_size or 0, float(a.st_mtime or 0), is_dir, is_link,
                             statmod.filemode(mode), owner or f"{a.st_uid} {a.st_gid}"))
        self.log("Status", f'Listing directory {path} successful')
        return out

    def mkdir(self, path):
        self._call(self.sftp.mkdir, path)

    def rmdir(self, path):
        self._call(self.sftp.rmdir, path)

    def delete(self, path):
        self._call(self.sftp.remove, path)

    def rename(self, old, new):
        self._call(self.sftp.posix_rename if "posix-rename@openssh.com" in str(getattr(self.sftp, "_exts", ""))
                   else self.sftp.rename, old, new)

    def chmod(self, path, mode):
        self._call(self.sftp.chmod, path, mode)

    def size(self, path):
        try:
            return self.sftp.stat(path).st_size
        except OSError:
            return None

    def download(self, remote, local, offset=0, progress=None, cancelled=None):
        self.log("Status", f'Starting download of {remote}' + (f" (resuming at {offset:,} bytes)" if offset else ""))
        with self._call(self.sftp.open, remote, "rb") as src, open(local, "ab" if offset else "wb") as dst:
            src.seek(offset)
            src.prefetch()
            while True:
                if cancelled and cancelled():
                    raise RemoteError("Cancelled")
                chunk = src.read(BLOCK)
                if not chunk:
                    break
                dst.write(chunk)
                if progress:
                    progress(len(chunk))
        self.log("Status", "File transfer successful")

    def upload(self, local, remote, offset=0, progress=None, cancelled=None):
        self.log("Status", f'Starting upload of {local}' + (f" (resuming at {offset:,} bytes)" if offset else ""))
        with open(local, "rb") as src, self._call(self.sftp.open, remote, "ab" if offset else "wb") as dst:
            dst.set_pipelined(True)
            src.seek(offset)
            while True:
                if cancelled and cancelled():
                    raise RemoteError("Cancelled")
                chunk = src.read(BLOCK)
                if not chunk:
                    break
                dst.write(chunk)
                if progress:
                    progress(len(chunk))
        self.log("Status", "File transfer successful")


def _load_key(path: str, passphrase: str = ""):
    import paramiko
    errors = []
    for cls in (paramiko.Ed25519Key, paramiko.ECDSAKey, paramiko.RSAKey):
        try:
            return cls.from_private_key_file(path, password=passphrase or None)
        except paramiko.PasswordRequiredException as e:
            raise RemoteError("The key file needs its passphrase (put it in Password).") from e
        except (paramiko.SSHException, ValueError) as e:
            errors.append(str(e))
    if open(path, "rb").read(32).startswith(b"PuTTY-User-Key-File"):
        raise RemoteError("This is a PuTTY .ppk key: export it as OpenSSH from PuTTYgen (Conversions menu).")
    raise RemoteError(f"Can't read the key file: {errors[-1] if errors else path}")


def _read_known(path: str) -> dict:
    out = {}
    if path and os.path.exists(path):
        for line in open(path, encoding="utf-8"):
            parts = line.split()
            if len(parts) >= 3 and not line.startswith("#"):
                out[(parts[0], parts[1])] = parts[2]
    return out


def _write_known(path: str, known: dict) -> None:
    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        for (host, kind), b64 in sorted(known.items()):
            fh.write(f"{host} {kind} {b64}\n")


def open_session(site: Site, log=None, host_key_check=None, known_hosts: str = "", cert_check=None):
    """A connected session for the site."""
    if site.protocol == "sftp":
        sess = SFTPSession(site, log, host_key_check, known_hosts)
    else:
        sess = FTPSession(site, log, cert_check)
    sess.connect()
    return sess


def join(folder: str, name: str) -> str:
    return posixpath.join(folder or "/", name)


def parent(path: str) -> str:
    p = posixpath.dirname(path.rstrip("/")) or "/"
    return p


def fmt_time(t: float) -> str:
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(t)) if t else ""
