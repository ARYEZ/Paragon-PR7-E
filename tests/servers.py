"""Real servers for the tests, on 127.0.0.1: FTP (plain, and with explicit TLS) via pyftpdlib, SFTP via paramiko's
server side mapped onto a folder. Test-only: PR7-E itself never runs a server (the servers come from PR7-F's tests)."""

from __future__ import annotations

import datetime
import os
import socket
import threading

import paramiko
from paramiko import SFTPAttributes, SFTPHandle, SFTPServer, SFTPServerInterface, ServerInterface
from paramiko.sftp import SFTP_FAILURE, SFTP_NO_SUCH_FILE, SFTP_OK, SFTP_PERMISSION_DENIED

USER, PASSWORD = "paragon", "s3cret pass"


# ------------------------------------------------------------------------------------------ FTP
def self_signed(folder: str) -> str:
    """A throwaway certificate for the FTPS server (what most small servers have)."""
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes, serialization
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.x509.oid import NameOID
    key = ec.generate_private_key(ec.SECP256R1())
    name = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "localhost")])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
            .serial_number(x509.random_serial_number()).not_valid_before(now - datetime.timedelta(days=1))
            .not_valid_after(now + datetime.timedelta(days=30)).sign(key, hashes.SHA256()))
    path = os.path.join(folder, "server.pem")
    with open(path, "wb") as fh:
        fh.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.TraditionalOpenSSL,
                                   serialization.NoEncryption()))
        fh.write(cert.public_bytes(serialization.Encoding.PEM))
    return path


class FTPServer:
    def __init__(self, root: str, tls: bool, certdir: str, tls_required: bool = False):
        from pyftpdlib.authorizers import DummyAuthorizer
        from pyftpdlib.handlers import FTPHandler, TLS_FTPHandler
        from pyftpdlib.servers import FTPServer as AsyncFTPServer
        auth = DummyAuthorizer()
        auth.add_user(USER, PASSWORD, root, perm="elradfmwMT")
        if tls:
            handler = type("H", (TLS_FTPHandler,), {})
            handler.certfile = self_signed(certdir)
            handler.tls_control_required = tls_required
            handler.tls_data_required = tls_required
        else:
            handler = type("H", (FTPHandler,), {})
        handler.authorizer = auth
        handler.banner = "Paragon test FTP server ready."
        handler.permit_foreign_addresses = True
        handler.auth_failed_timeout = 0.1  # the "530" for a wrong password comes quickly
        self.server = AsyncFTPServer(("127.0.0.1", 0), handler)
        self.port = self.server.socket.getsockname()[1]
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"timeout": 0.05}, daemon=True)
        self.thread.start()

    def stop(self):
        self.server.close_all()


# ------------------------------------------------------------------------------------------ SFTP
class _Handle(SFTPHandle):
    def stat(self):
        try:
            return SFTPAttributes.from_stat(os.fstat(self.readfile.fileno()))
        except OSError as e:
            return SFTPServer.convert_errno(e.errno)

    def chattr(self, attr):
        return SFTP_OK


class _FS(SFTPServerInterface):
    root = "/"

    def __init__(self, server, *args, **kwargs):
        super().__init__(server, *args, **kwargs)

    def _real(self, path):
        return os.path.join(self.root, self.canonicalize(path).lstrip("/"))

    def canonicalize(self, path):
        import posixpath
        return posixpath.normpath("/" + path) if not path.startswith("/") else posixpath.normpath(path)

    def list_folder(self, path):
        real = self._real(path)
        try:
            out = []
            for name in os.listdir(real):
                a = SFTPAttributes.from_stat(os.lstat(os.path.join(real, name)))
                a.filename = name
                out.append(a)
            return out
        except OSError as e:
            return SFTPServer.convert_errno(e.errno)

    def stat(self, path):
        try:
            return SFTPAttributes.from_stat(os.stat(self._real(path)))
        except OSError as e:
            return SFTPServer.convert_errno(e.errno)

    lstat = stat

    def open(self, path, flags, attr):
        real = self._real(path)
        try:
            binflag = getattr(os, "O_BINARY", 0)
            fd = os.open(real, flags | binflag, 0o644)
        except OSError as e:
            return SFTPServer.convert_errno(e.errno)
        if flags & os.O_WRONLY:
            mode = "ab" if flags & os.O_APPEND else "wb"
        elif flags & os.O_RDWR:
            mode = "a+b" if flags & os.O_APPEND else "r+b"
        else:
            mode = "rb"
        f = os.fdopen(fd, mode)
        h = _Handle(flags)
        h.filename = real
        h.readfile = f
        h.writefile = f
        return h

    def remove(self, path):
        try:
            os.remove(self._real(path))
        except OSError as e:
            return SFTPServer.convert_errno(e.errno)
        return SFTP_OK

    def rename(self, old, new):
        if os.path.exists(self._real(new)):
            return SFTP_FAILURE
        try:
            os.rename(self._real(old), self._real(new))
        except OSError as e:
            return SFTPServer.convert_errno(e.errno)
        return SFTP_OK

    def mkdir(self, path, attr):
        try:
            os.mkdir(self._real(path))
        except OSError as e:
            return SFTPServer.convert_errno(e.errno)
        return SFTP_OK

    def rmdir(self, path):
        try:
            os.rmdir(self._real(path))
        except OSError as e:
            return SFTPServer.convert_errno(e.errno)
        return SFTP_OK

    def chattr(self, path, attr):
        try:
            if attr.st_mode is not None:
                os.chmod(self._real(path), attr.st_mode & 0o7777)
        except OSError as e:
            return SFTPServer.convert_errno(e.errno)
        return SFTP_OK


class _Auth(ServerInterface):
    def __init__(self, pubkey=None):
        self.pubkey = pubkey

    def check_auth_password(self, username, password):
        return paramiko.AUTH_SUCCESSFUL if (username, password) == (USER, PASSWORD) else paramiko.AUTH_FAILED

    def check_auth_publickey(self, username, key):
        ok = self.pubkey is not None and key.asbytes() == self.pubkey.asbytes() and username == USER
        return paramiko.AUTH_SUCCESSFUL if ok else paramiko.AUTH_FAILED

    def get_allowed_auths(self, username):
        return "password,publickey"

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED


class SFTPServerThread:
    def __init__(self, root: str, pubkey=None):
        self.host_key = paramiko.ECDSAKey.generate()
        self.root, self.pubkey = root, pubkey
        self.sock = socket.socket()
        self.sock.bind(("127.0.0.1", 0))
        self.sock.listen(8)
        self.port = self.sock.getsockname()[1]
        self.running = True
        self.transports = []
        threading.Thread(target=self._serve, daemon=True).start()

    def _serve(self):
        fs = type("FS", (_FS,), {"root": self.root})
        while self.running:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                return
            t = paramiko.Transport(conn)
            t.add_server_key(self.host_key)
            t.set_subsystem_handler("sftp", SFTPServer, fs)
            try:
                t.start_server(server=_Auth(self.pubkey))
            except Exception:  # noqa: BLE001 - a client that went away
                continue
            self.transports.append(t)

    def stop(self):
        self.running = False
        self.sock.close()
        for t in self.transports:
            t.close()
