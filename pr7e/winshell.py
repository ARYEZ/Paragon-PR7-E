"""The Windows shell through COM, with ctypes (no extra packages), Windows only:

- `thumbnail(path, size)`: the thumbnail Explorer shows (IShellItemImageFactory), so videos, PDFs, Office files
  and any type with a thumbnail handler get one, from Windows' own thumbnail cache.
- `show_menu(paths, ...)`: the full Windows right-click menu for items ("Show More Options"), built by the same
  IContextMenu interface Explorer uses: other programs' items (7-Zip / PR7-Z, PR7-W, PR7-L, Send to, Defender...)
  are all there. Submenus such as Send to are filled while the menu is open, by messages the menu's owner window
  must pass to IContextMenu2/3: a small hidden window does that.
- `menu_items(paths)`: the menu's item names without showing it (tests).

COM is set up per thread (STA). Every interface is released and every PIDL freed in `finally` blocks.
"""

from __future__ import annotations

import os
import sys
import threading
import uuid

available = sys.platform == "win32"

if available:
    import ctypes
    from ctypes import (HRESULT, POINTER, WINFUNCTYPE, Structure, byref, c_int, c_long, c_ubyte, c_uint, c_ulong,
                        c_ushort, c_void_p, c_wchar_p, sizeof)
    from ctypes import wintypes

    ole32 = ctypes.windll.ole32
    shell32 = ctypes.windll.shell32
    user32 = ctypes.windll.user32
    gdi32 = ctypes.windll.gdi32
    kernel32 = ctypes.windll.kernel32
    LRESULT = ctypes.c_ssize_t

    class GUID(Structure):
        _fields_ = [("Data1", c_ulong), ("Data2", c_ushort), ("Data3", c_ushort), ("Data4", c_ubyte * 8)]

    def _guid(s: str) -> GUID:
        u = uuid.UUID(s)
        return GUID(u.time_low, u.time_mid, u.time_hi_version, (c_ubyte * 8)(*u.bytes[8:]))

    IID_IShellItemImageFactory = _guid("bcc18b79-ba16-442f-80c4-8a59c30c463b")
    IID_IShellFolder = _guid("000214E6-0000-0000-C000-000000000046")
    IID_IContextMenu = _guid("000214e4-0000-0000-c000-000000000046")
    IID_IContextMenu2 = _guid("000214f4-0000-0000-c000-000000000046")
    IID_IContextMenu3 = _guid("bcfce0a0-ec17-11d0-8d10-00a0c90f2719")

    class SIZE(Structure):
        _fields_ = [("cx", c_long), ("cy", c_long)]

    class POINT(Structure):
        _fields_ = [("x", c_long), ("y", c_long)]

    class BITMAP(Structure):
        _fields_ = [("bmType", c_long), ("bmWidth", c_long), ("bmHeight", c_long), ("bmWidthBytes", c_long),
                    ("bmPlanes", c_ushort), ("bmBitsPixel", c_ushort), ("bmBits", c_void_p)]

    class BITMAPINFOHEADER(Structure):
        _fields_ = [("biSize", wintypes.DWORD), ("biWidth", c_long), ("biHeight", c_long), ("biPlanes", wintypes.WORD),
                    ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD), ("biSizeImage", wintypes.DWORD),
                    ("biXPelsPerMeter", c_long), ("biYPelsPerMeter", c_long), ("biClrUsed", wintypes.DWORD),
                    ("biClrImportant", wintypes.DWORD)]

    class CMINVOKECOMMANDINFOEX(Structure):
        _fields_ = [("cbSize", wintypes.DWORD), ("fMask", wintypes.DWORD), ("hwnd", wintypes.HWND),
                    ("lpVerb", c_void_p), ("lpParameters", c_void_p), ("lpDirectory", c_void_p), ("nShow", c_int),
                    ("dwHotKey", wintypes.DWORD), ("hIcon", wintypes.HANDLE), ("lpTitle", c_void_p),
                    ("lpVerbW", c_void_p), ("lpParametersW", c_void_p), ("lpDirectoryW", c_void_p),
                    ("lpTitleW", c_void_p), ("ptInvoke", POINT)]

    WNDPROC = WINFUNCTYPE(LRESULT, wintypes.HWND, c_uint, wintypes.WPARAM, wintypes.LPARAM)

    class WNDCLASSW(Structure):
        _fields_ = [("style", c_uint), ("lpfnWndProc", WNDPROC), ("cbClsExtra", c_int), ("cbWndExtra", c_int),
                    ("hInstance", wintypes.HINSTANCE), ("hIcon", wintypes.HICON), ("hCursor", c_void_p),
                    ("hbrBackground", wintypes.HBRUSH), ("lpszMenuName", c_wchar_p), ("lpszClassName", c_wchar_p)]

    shell32.SHCreateItemFromParsingName.argtypes = [c_wchar_p, c_void_p, POINTER(GUID), POINTER(c_void_p)]
    shell32.SHCreateItemFromParsingName.restype = HRESULT
    shell32.SHParseDisplayName.argtypes = [c_wchar_p, c_void_p, POINTER(c_void_p), c_ulong, POINTER(c_ulong)]
    shell32.SHParseDisplayName.restype = HRESULT
    shell32.SHBindToParent.argtypes = [c_void_p, POINTER(GUID), POINTER(c_void_p), POINTER(c_void_p)]
    shell32.SHBindToParent.restype = HRESULT
    shell32.ILFindLastID.argtypes = [c_void_p]
    shell32.ILFindLastID.restype = c_void_p
    ole32.CoTaskMemFree.argtypes = [c_void_p]
    ole32.CoInitializeEx.argtypes = [c_void_p, wintypes.DWORD]
    gdi32.GetObjectW.argtypes = [c_void_p, c_int, c_void_p]
    gdi32.DeleteObject.argtypes = [c_void_p]
    gdi32.GetDIBits.argtypes = [c_void_p, c_void_p, c_uint, c_uint, c_void_p, c_void_p, c_uint]
    user32.GetDC.argtypes = [c_void_p]
    user32.GetDC.restype = c_void_p
    user32.ReleaseDC.argtypes = [c_void_p, c_void_p]
    user32.CreatePopupMenu.restype = c_void_p
    user32.DestroyMenu.argtypes = [c_void_p]
    user32.GetMenuItemCount.argtypes = [c_void_p]
    user32.GetMenuStringW.argtypes = [c_void_p, c_uint, c_wchar_p, c_int, c_uint]
    user32.GetSubMenu.argtypes = [c_void_p, c_int]
    user32.GetSubMenu.restype = c_void_p
    user32.TrackPopupMenuEx.argtypes = [c_void_p, c_uint, c_int, c_int, wintypes.HWND, c_void_p]
    user32.TrackPopupMenuEx.restype = c_int
    user32.DefWindowProcW.argtypes = [wintypes.HWND, c_uint, wintypes.WPARAM, wintypes.LPARAM]
    user32.DefWindowProcW.restype = LRESULT
    user32.CreateWindowExW.argtypes = [wintypes.DWORD, c_wchar_p, c_wchar_p, wintypes.DWORD, c_int, c_int, c_int, c_int,
                                       wintypes.HWND, c_void_p, wintypes.HINSTANCE, c_void_p]
    user32.CreateWindowExW.restype = wintypes.HWND
    user32.PostMessageW.argtypes = [wintypes.HWND, c_uint, wintypes.WPARAM, wintypes.LPARAM]
    user32.SetForegroundWindow.argtypes = [wintypes.HWND]
    user32.GetCursorPos.argtypes = [POINTER(POINT)]
    kernel32.GetModuleHandleW.restype = wintypes.HINSTANCE

    def _method(obj, index: int, restype, *argtypes):
        """Method `index` of a COM interface pointer (IUnknown is 0-2)."""
        vtbl = ctypes.cast(obj, POINTER(POINTER(c_void_p))).contents
        fn = WINFUNCTYPE(restype, c_void_p, *argtypes)(vtbl[index])
        return lambda *a: fn(obj, *a)

    def _release(obj) -> None:
        if obj:
            _method(obj, 2, c_ulong)()

    def _query(obj, iid):
        out = c_void_p()
        try:
            _method(obj, 0, HRESULT, POINTER(GUID), POINTER(c_void_p))(byref(iid), byref(out))
        except OSError:
            return None
        return out


_tls = threading.local()


def com_init() -> None:
    """COM for this thread (an STA, as the shell's thumbnail and menu handlers expect). Qt's GUI thread already has
    OLE: the call just returns S_FALSE there."""
    if available and not getattr(_tls, "com", False):
        ole32.CoInitializeEx(None, 0x2 | 0x4)  # COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE
        _tls.com = True


# --------------------------------------------------------------------------- thumbnails
SIIGBF_BIGGERSIZEOK, SIIGBF_THUMBNAILONLY = 0x1, 0x8


def thumbnail(path: str, size: int = 256):
    """(width, height, BGRA bytes, has_alpha) of the thumbnail Windows has for the file, or None (no handler, or
    the file has only an icon)."""
    if not available:
        return None
    com_init()
    fac = c_void_p()
    try:
        shell32.SHCreateItemFromParsingName(os.path.abspath(path), None, byref(IID_IShellItemImageFactory),
                                            byref(fac))
    except OSError:
        return None
    hbm = c_void_p()
    try:
        _method(fac, 3, HRESULT, SIZE, c_int, POINTER(c_void_p))(SIZE(size, size),
                                                                SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, byref(hbm))
    except OSError:
        return None
    finally:
        _release(fac)
    try:
        return _bitmap_bytes(hbm)
    finally:
        gdi32.DeleteObject(hbm)


def _bitmap_bytes(hbm):
    bm = BITMAP()
    if not gdi32.GetObjectW(hbm, sizeof(bm), byref(bm)):
        return None
    w, h = bm.bmWidth, abs(bm.bmHeight)
    if w <= 0 or h <= 0:
        return None
    bi = BITMAPINFOHEADER(sizeof(BITMAPINFOHEADER), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)  # top-down, 32 bpp
    buf = (c_ubyte * (w * h * 4))()
    hdc = user32.GetDC(None)
    try:
        lines = gdi32.GetDIBits(hdc, hbm, 0, h, buf, byref(bi), 0)
    finally:
        user32.ReleaseDC(None, hdc)
    if lines != h:
        return None
    data = bytes(buf)
    has_alpha = any(data[i] for i in range(3, len(data), 4 * 97))  # sampled: an all-zero alpha means opaque
    return w, h, data, has_alpha


# --------------------------------------------------------------------------- the Windows right-click menu
CMF_NORMAL, CMF_EXPLORE, CMF_CANRENAME, CMF_EXTENDEDVERBS = 0x0, 0x4, 0x10, 0x100
FIRST, LAST = 1, 0x7FFF


def same_folder(paths: list[str]) -> bool:
    """The shell builds one menu for items in one folder."""
    return len({os.path.normcase(os.path.dirname(os.path.abspath(p))) for p in paths}) == 1


def _context_menu(paths: list[str], hwnd=None):
    """(IContextMenu, PIDLs to free) for items that share a folder."""
    com_init()
    pidls = []
    psf = c_void_p()
    try:
        for p in paths:
            pidl = c_void_p()
            shell32.SHParseDisplayName(os.path.abspath(p), None, byref(pidl), 0, None)
            pidls.append(pidl)
        child = c_void_p()
        shell32.SHBindToParent(pidls[0], byref(IID_IShellFolder), byref(psf), byref(child))
        arr = (c_void_p * len(pidls))(*[shell32.ILFindLastID(pd) for pd in pidls])
        pcm = c_void_p()
        _method(psf, 10, HRESULT, wintypes.HWND, c_uint, c_void_p, POINTER(GUID), c_void_p, POINTER(c_void_p))(
            hwnd, len(pidls), arr, byref(IID_IContextMenu), None, byref(pcm))
        return pcm, pidls
    except OSError:
        for pd in pidls:
            ole32.CoTaskMemFree(pd)
        raise
    finally:
        _release(psf)


def _names(hmenu, depth=0) -> list[str]:
    out = []
    buf = ctypes.create_unicode_buffer(512)
    for i in range(user32.GetMenuItemCount(hmenu)):
        n = user32.GetMenuStringW(hmenu, i, buf, 512, 0x400)  # MF_BYPOSITION
        if n:
            out.append(("  " * depth) + buf.value.replace("&", ""))
        sub = user32.GetSubMenu(hmenu, i)
        if sub and depth < 1:
            out.extend(_names(sub, depth + 1))
    return out


def menu_items(paths: list[str], extended: bool = False) -> list[str]:
    """The names in the Windows menu for these items, without showing it (tests)."""
    pcm, pidls = _context_menu(paths)
    hmenu = user32.CreatePopupMenu()
    try:
        _method(pcm, 3, HRESULT, c_void_p, c_uint, c_uint, c_uint, c_uint)(
            hmenu, 0, FIRST, LAST, CMF_NORMAL | CMF_EXPLORE | (CMF_EXTENDEDVERBS if extended else 0))
        return _names(hmenu)
    finally:
        user32.DestroyMenu(hmenu)
        _release(pcm)
        for pd in pidls:
            ole32.CoTaskMemFree(pd)


class _MenuHost:
    """The hidden window that owns the menu while it's open and passes the submenu messages to IContextMenu2/3."""

    def __init__(self):
        self.cm2 = self.cm3 = None
        self.proc = WNDPROC(self._wndproc)
        self.hwnd = None

    def _wndproc(self, hwnd, msg, wp, lp):
        if msg in (0x117, 0x2B, 0x2C, 0x120, 0x11F):  # INITMENUPOPUP, DRAWITEM, MEASUREITEM, MENUCHAR, MENUSELECT
            try:
                if self.cm3:
                    res = LRESULT()
                    _method(self.cm3, 7, HRESULT, c_uint, wintypes.WPARAM, wintypes.LPARAM, POINTER(LRESULT))(
                        msg, wp, lp, byref(res))
                    return res.value
                if self.cm2:
                    _method(self.cm2, 6, HRESULT, c_uint, wintypes.WPARAM, wintypes.LPARAM)(msg, wp, lp)
                    return 0
            except OSError:
                pass
        return user32.DefWindowProcW(hwnd, msg, wp, lp)

    def window(self, owner):
        if self.hwnd:
            return self.hwnd
        inst = kernel32.GetModuleHandleW(None)
        wc = WNDCLASSW()
        wc.lpfnWndProc = self.proc
        wc.hInstance = inst
        wc.lpszClassName = "PR7E.ShellMenuHost"
        user32.RegisterClassW(byref(wc))  # fails harmlessly if already registered
        self.hwnd = user32.CreateWindowExW(0, wc.lpszClassName, "", 0x80000000, 0, 0, 0, 0, owner, None, inst, None)
        return self.hwnd


_host = None


def show_menu(paths: list[str], owner: int = 0, extended: bool = False, folder: str = "") -> bool:
    """Show the Windows menu at the mouse and run what's picked. Returns True if something was picked."""
    global _host
    if not available or not paths or not same_folder(paths):
        return False
    _host = _host or _MenuHost()
    hwnd = _host.window(owner)
    pcm, pidls = _context_menu(paths, hwnd)
    hmenu = user32.CreatePopupMenu()
    try:
        _method(pcm, 3, HRESULT, c_void_p, c_uint, c_uint, c_uint, c_uint)(
            hmenu, 0, FIRST, LAST, CMF_NORMAL | CMF_EXPLORE | CMF_CANRENAME | (CMF_EXTENDEDVERBS if extended else 0))
        _host.cm3 = _query(pcm, IID_IContextMenu3)
        _host.cm2 = None if _host.cm3 else _query(pcm, IID_IContextMenu2)
        pt = POINT()
        user32.GetCursorPos(byref(pt))
        user32.SetForegroundWindow(hwnd)
        cmd = user32.TrackPopupMenuEx(hmenu, 0x100 | 0x2, pt.x, pt.y, hwnd, None)  # TPM_RETURNCMD | RIGHTBUTTON
        user32.PostMessageW(hwnd, 0, 0, 0)
        if cmd < FIRST:
            return False
        directory = ctypes.create_unicode_buffer(folder or os.path.dirname(os.path.abspath(paths[0])))
        info = CMINVOKECOMMANDINFOEX()
        info.cbSize = sizeof(info)
        info.fMask = 0x4000 | 0x20000000  # CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE
        info.hwnd = owner or hwnd
        info.lpVerb = info.lpVerbW = cmd - FIRST  # MAKEINTRESOURCE(offset)
        info.lpDirectoryW = ctypes.cast(directory, c_void_p).value
        info.nShow = 1  # SW_SHOWNORMAL
        info.ptInvoke = pt
        try:
            _method(pcm, 4, HRESULT, c_void_p)(byref(info))
        except OSError:
            return False
        return True
    finally:
        for cm in (_host.cm3, _host.cm2):
            _release(cm)
        _host.cm2 = _host.cm3 = None
        user32.DestroyMenu(hmenu)
        _release(pcm)
        for pd in pidls:
            ole32.CoTaskMemFree(pd)


# --------------------------------------------------------------------------- the Network folder
FOLDERID_NETWORK = "D20BEEC4-5CA8-4905-AE3B-BF251EA09B53"
SHCONTF_FOLDERS, SHCONTF_NONFOLDERS = 0x20, 0x40
SHGDN_NORMAL, SHGDN_FORPARSING = 0x0, 0x8000


def network_items() -> list[tuple[str, str]]:
    """(name, parsing name) of what Explorer's Network page shows (computers are "\\\\\\\\NAME"; media devices and
    routers have other parsing names). Windows finds them (WSD, NetBIOS, SSDP), which can take a few seconds."""
    if not available:
        return []
    com_init()
    shell32.SHGetKnownFolderIDList.argtypes = [POINTER(GUID), wintypes.DWORD, c_void_p, POINTER(c_void_p)]
    shell32.SHGetKnownFolderIDList.restype = HRESULT
    shell32.SHBindToObject.argtypes = [c_void_p, c_void_p, c_void_p, POINTER(GUID), POINTER(c_void_p)]
    shell32.SHBindToObject.restype = HRESULT
    shlwapi = ctypes.windll.shlwapi
    shlwapi.StrRetToBufW.argtypes = [c_void_p, c_void_p, c_wchar_p, c_uint]
    shlwapi.StrRetToBufW.restype = HRESULT
    pidl, psf, enum = c_void_p(), c_void_p(), c_void_p()
    out = []
    try:
        shell32.SHGetKnownFolderIDList(byref(_guid(FOLDERID_NETWORK)), 0, None, byref(pidl))
        shell32.SHBindToObject(None, pidl, None, byref(IID_IShellFolder), byref(psf))
        _method(psf, 4, HRESULT, wintypes.HWND, wintypes.DWORD, POINTER(c_void_p))(
            None, SHCONTF_FOLDERS | SHCONTF_NONFOLDERS, byref(enum))
        if not enum:
            return []
        nxt = _method(enum, 3, HRESULT, c_ulong, POINTER(c_void_p), POINTER(c_ulong))
        display = _method(psf, 11, HRESULT, c_void_p, wintypes.DWORD, c_void_p)
        strret = (ctypes.c_ubyte * 272)()   # STRRET: a UINT and a 260-byte union
        buf = ctypes.create_unicode_buffer(1024)
        while True:
            child, got = c_void_p(), c_ulong()
            if nxt(1, byref(child), byref(got)) != 0 or not got.value:
                break
            try:
                names = []
                for flags in (SHGDN_NORMAL, SHGDN_FORPARSING):
                    display(child, flags, strret)
                    shlwapi.StrRetToBufW(strret, child, buf, 1024)
                    names.append(buf.value)
                out.append((names[0], names[1]))
            except OSError:
                pass
            finally:
                ole32.CoTaskMemFree(child)
    except OSError:
        return out
    finally:
        _release(enum)
        _release(psf)
        if pidl:
            ole32.CoTaskMemFree(pidl)
    return out


def computer_shares(computer: str, hidden: bool = False) -> list[tuple[str, str]]:
    """(share name, remark) of a computer's shared folders (NetShareEnum, level 1): disk shares only; the hidden
    ones (C$, ADMIN$, names ending in $) only with `hidden`. Raises OSError (5 = access denied: log in first)."""
    if not available:
        return []

    class SHARE_INFO_1(Structure):
        _fields_ = [("netname", c_wchar_p), ("type", wintypes.DWORD), ("remark", c_wchar_p)]
    netapi = ctypes.windll.netapi32
    netapi.NetShareEnum.argtypes = [c_wchar_p, wintypes.DWORD, POINTER(c_void_p), wintypes.DWORD,
                                    POINTER(wintypes.DWORD), POINTER(wintypes.DWORD), POINTER(wintypes.DWORD)]
    netapi.NetApiBufferFree.argtypes = [c_void_p]
    buf, read, total, resume = c_void_p(), wintypes.DWORD(), wintypes.DWORD(), wintypes.DWORD()
    res = netapi.NetShareEnum(computer, 1, byref(buf), 0xFFFFFFFF, byref(read), byref(total), byref(resume))
    try:
        if res not in (0, 234):  # NERR_Success, ERROR_MORE_DATA
            raise OSError(res, {5: "Access is denied.", 53: "The computer can’t be found.",
                                1326: "The user name or password is wrong."}.get(res, ctypes.FormatError(res).strip()),
                          computer)
        out = []
        if buf:
            items = ctypes.cast(buf, POINTER(SHARE_INFO_1 * read.value)).contents
            for it in items:
                disk = (it.type & 0xFF) == 0           # STYPE_DISKTREE
                special = bool(it.type & 0x80000000) or (it.netname or "").endswith("$")
                if disk and (hidden or not special):
                    out.append((it.netname, it.remark or ""))
        return sorted(out, key=lambda x: x[0].lower())
    finally:
        if buf:
            netapi.NetApiBufferFree(buf)


# --------------------------------------------------------------------------- the folder's own menu (empty space)
def _folder_view_menu(folder: str, hwnd=None):
    """The folder's background menu (what Explorer shows on empty space, with New), from its IShellFolder's
    CreateViewObject(IID_IContextMenu)."""
    com_init()
    shell32.SHBindToObject.argtypes = [c_void_p, c_void_p, c_void_p, POINTER(GUID), POINTER(c_void_p)]
    shell32.SHBindToObject.restype = HRESULT
    pidl, psf, pcm = c_void_p(), c_void_p(), c_void_p()
    try:
        shell32.SHParseDisplayName(os.path.abspath(folder), None, byref(pidl), 0, None)
        shell32.SHBindToObject(None, pidl, None, byref(IID_IShellFolder), byref(psf))
        _method(psf, 8, HRESULT, wintypes.HWND, POINTER(GUID), POINTER(c_void_p))(hwnd, byref(IID_IContextMenu),
                                                                              byref(pcm))
        return pcm
    finally:
        _release(psf)
        if pidl:
            ole32.CoTaskMemFree(pidl)


def background_menu_items(folder: str) -> list[str]:
    """The names in the folder's Windows menu, without showing it (tests)."""
    pcm = _folder_view_menu(folder)
    hmenu = user32.CreatePopupMenu()
    try:
        _method(pcm, 3, HRESULT, c_void_p, c_uint, c_uint, c_uint, c_uint)(hmenu, 0, FIRST, LAST,
                                                                           CMF_NORMAL | CMF_EXPLORE)
        cm3 = _query(pcm, IID_IContextMenu3)  # New fills its submenu when asked
        if cm3:
            for i in range(user32.GetMenuItemCount(hmenu)):
                sub = user32.GetSubMenu(hmenu, i)
                if sub:
                    try:
                        _method(cm3, 7, HRESULT, c_uint, wintypes.WPARAM, wintypes.LPARAM, POINTER(LRESULT))(
                            0x117, sub, i, byref(LRESULT()))
                    except OSError:
                        pass
            _release(cm3)
        return _names(hmenu)
    finally:
        user32.DestroyMenu(hmenu)
        _release(pcm)


def show_background_menu(folder: str, owner: int = 0) -> bool:
    """Show the folder's Windows menu (New, Paste shortcut...) at the mouse and run what's picked."""
    global _host
    if not available or not os.path.isdir(folder):
        return False
    _host = _host or _MenuHost()
    hwnd = _host.window(owner)
    pcm = _folder_view_menu(folder, hwnd)
    hmenu = user32.CreatePopupMenu()
    try:
        _method(pcm, 3, HRESULT, c_void_p, c_uint, c_uint, c_uint, c_uint)(hmenu, 0, FIRST, LAST,
                                                                           CMF_NORMAL | CMF_EXPLORE)
        _host.cm3 = _query(pcm, IID_IContextMenu3)
        _host.cm2 = None if _host.cm3 else _query(pcm, IID_IContextMenu2)
        pt = POINT()
        user32.GetCursorPos(byref(pt))
        user32.SetForegroundWindow(hwnd)
        cmd = user32.TrackPopupMenuEx(hmenu, 0x100 | 0x2, pt.x, pt.y, hwnd, None)
        user32.PostMessageW(hwnd, 0, 0, 0)
        if cmd < FIRST:
            return False
        directory = ctypes.create_unicode_buffer(os.path.abspath(folder))
        info = CMINVOKECOMMANDINFOEX()
        info.cbSize = sizeof(info)
        info.fMask = 0x4000 | 0x20000000
        info.hwnd = owner or hwnd
        info.lpVerb = info.lpVerbW = cmd - FIRST
        info.lpDirectoryW = ctypes.cast(directory, c_void_p).value
        info.nShow = 1
        info.ptInvoke = pt
        try:
            _method(pcm, 4, HRESULT, c_void_p)(byref(info))
        except OSError:
            return False
        return True
    finally:
        for cm in (_host.cm3, _host.cm2):
            _release(cm)
        _host.cm2 = _host.cm3 = None
        user32.DestroyMenu(hmenu)
        _release(pcm)


# --------------------------------------------------------------------------- Open With: the apps for a type
IID_IShellItem = None
if available:
    IID_IShellItem = _guid("43826d1e-e718-42ee-bc55-a1e261c37bfe")
    IID_IDataObject = _guid("0000010e-0000-0000-C000-000000000046")
    BHID_DataObject = _guid("B8C0BD9F-ED24-455c-83E6-D5390C4FE8C4")


def _co_str(fn) -> str:
    out = c_wchar_p()
    fn(byref(out))
    try:
        return out.value or ""
    finally:
        ole32.CoTaskMemFree(ctypes.cast(out, c_void_p))


def open_with_apps(path: str) -> list[tuple[str, str]]:
    """(name shown, handler id) of the apps Windows recommends for the file's type (SHAssocEnumHandlers): the list
    in Explorer's Open with submenu."""
    if not available:
        return []
    com_init()
    ext = os.path.splitext(path)[1] or "*"
    shell32.SHAssocEnumHandlers.argtypes = [c_wchar_p, c_int, POINTER(c_void_p)]
    shell32.SHAssocEnumHandlers.restype = HRESULT
    enum = c_void_p()
    out, seen = [], set()
    try:
        shell32.SHAssocEnumHandlers(ext, 1, byref(enum))  # ASSOC_FILTER_RECOMMENDED
        nxt = _method(enum, 3, HRESULT, c_ulong, POINTER(c_void_p), POINTER(c_ulong))
        while True:
            h, got = c_void_p(), c_ulong()
            if nxt(1, byref(h), byref(got)) != 0 or not got.value:
                break
            try:
                ui = _co_str(_method(h, 4, HRESULT, POINTER(c_wchar_p)))
                name = _co_str(_method(h, 3, HRESULT, POINTER(c_wchar_p)))
                if ui and name.lower() not in seen:
                    seen.add(name.lower())
                    out.append((ui, name))
            except OSError:
                pass
            finally:
                _release(h)
    except OSError:
        return out
    finally:
        _release(enum)
    return out


def open_with_app(path: str, handler: str) -> bool:
    """Open the file with one of open_with_apps' handlers (IAssocHandler::Invoke on the file's data object)."""
    if not available:
        return False
    com_init()
    ext = os.path.splitext(path)[1] or "*"
    enum, item, data = c_void_p(), c_void_p(), c_void_p()
    try:
        shell32.SHAssocEnumHandlers(ext, 0, byref(enum))  # ASSOC_FILTER_NONE: find it whatever its standing
        nxt = _method(enum, 3, HRESULT, c_ulong, POINTER(c_void_p), POINTER(c_ulong))
        shell32.SHCreateItemFromParsingName(os.path.abspath(path), None, byref(IID_IShellItem), byref(item))
        _method(item, 3, HRESULT, c_void_p, POINTER(GUID), POINTER(GUID), POINTER(c_void_p))(
            None, byref(BHID_DataObject), byref(IID_IDataObject), byref(data))
        while True:
            h, got = c_void_p(), c_ulong()
            if nxt(1, byref(h), byref(got)) != 0 or not got.value:
                return False
            try:
                if _co_str(_method(h, 3, HRESULT, POINTER(c_wchar_p))).lower() == handler.lower():
                    _method(h, 8, HRESULT, c_void_p)(data)
                    return True
            except OSError:
                return False
            finally:
                _release(h)
    except OSError:
        return False
    finally:
        _release(data)
        _release(item)
        _release(enum)


def open_with_dialog(path: str, owner: int = 0) -> bool:
    """Windows' "How do you want to open this file?" with "Always use this app" (SHOpenWithDialog): the only way
    Windows 10 / 11 let a default app be chosen, on purpose (so no program can take over a file type quietly)."""
    if not available:
        return False

    class OPENASINFO(Structure):
        _fields_ = [("pcszFile", c_wchar_p), ("pcszClass", c_wchar_p), ("oaifInFlags", c_int)]
    shell32.SHOpenWithDialog.argtypes = [wintypes.HWND, POINTER(OPENASINFO)]
    shell32.SHOpenWithDialog.restype = HRESULT
    info = OPENASINFO(os.path.abspath(path), None, 0x1 | 0x2 | 0x4)  # ALLOW_REGISTRATION | REGISTER_EXT | EXEC
    try:
        shell32.SHOpenWithDialog(owner or None, byref(info))
        return True
    except OSError:
        return False
