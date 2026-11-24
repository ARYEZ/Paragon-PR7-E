# Paragon PR7-E: notes for Claude

**Paragon PR7-E** is the owner's personal file manager: Python (**PySide6**) that **works the way GNOME Files
(Nautilus, Ubuntu's file manager) works**, in the Paragon family look (the PR7 plugins, Paragon Photo, PR7-Z
`ARYEZ/Paragon-PR7-Z`, PR7-W, PR7-L, PR7-F `ARYEZ/Paragon-PR7-F`). Windows 10/11 x64 is the target; it runs on
Linux for tests. The owner writes Python; explain the why of changes.

## How it's built

- `pr7e/fs.py` (no Qt): `Entry`, `list_dir` (hidden = dot files + Windows hidden/system attributes), Files' sorts
  (folders first, natural, accent-insensitive), `search` (every word, When/What filters, LOCATION relative to the
  root), Files' wording (`human_size` decimal units, `date_text`), names (`name_problem` uses Windows' rules on every
  system, `copy_name` "x (copy).ext", `free_name` "x (2).ext").
- `pr7e/ops.py` (no Qt): every file operation is a `Job` (CopyJob also moves; same drive = os.rename), run on a
  thread by `workers.JobRunner`, watched by the window (`fraction`, `detail`, `cancel`). Name clashes call
  `job.ask(Conflict)` -> ("replace"|"merge"|"skip", all) or ("rename", name). Each job sets `undo` to the job that
  reverses it; that job's `undo` is the redo (copy -> TrashJob -> RestoreJob ...).
- `pr7e/trash.py` (no Qt): Windows Recycle Bin (SHFileOperationW FOF_ALLOWUNDO to send; `$I`/`$R` pairs in
  `<drive>:\$Recycle.Bin\<SID>` to list/restore) or a freedesktop trash folder (Linux). **`PR7E_TRASH` points Trash
  at a folder: tests always set it** (conftest), so they never touch the real Recycle Bin; the one real-bin test runs
  only on CI.
- `pr7e/places.py` (no Qt): sidebar places; Windows known folders (SHGetKnownFolderPath); drives; bookmarks and
  stars in settings; Recent from `%APPDATA%\Microsoft\Windows\Recent\*.lnk` (`lnk_target` parses LinkInfo).
- `pr7e/system.py`: open (os.startfile + SHAddToRecentDocs), Windows' Open with chooser, default app / type names
  (AssocQueryStringW), terminal. `pr7e/shell.py` (HKCU only, through `Registry(prefix)` so tests use a stand-in or,
  on Windows CI, a throwaway key): "Open in PR7-E" verbs (Directory / Drive / Background) and "Use PR7-E for
  Folders" (Directory\shell and Drive\shell default = PR7E, Win+E via the CLSID {52205fd8...} opennewwindow
  override; never the shell's Folder class). Wanted state in `HKCU\Software\Paragon\PR7-E\Shell` (Menu, Folders)
  so each switch keeps the other; a previous default is saved and put back. CLI / installer: `--register-menu`,
  `--unregister-menu`, `--default-folders on|off`, `--unregister` (everything). `app.folders_from` turns Explorer's
  `C:"` (from "C:\\") into the drive root.
- `pr7e/archive.py` (no Qt): CompressJob / ExtractJob (ops.Job). 7-Zip found by `find_7z` (PR7E_7Z, PR7-Z's
  InstallLocation from its uninstall key + `_internal\engine\7z.exe`, installed 7-Zip, PATH); run with `-bsp1`
  for progress, `-p` always given (never a prompt), `ask_password` asked again on a wrong one. .zip / .tar.xz made by
  Python; .tar.* always extracted by tarfile (7-Zip peels one layer). Staging folder in the destination, then Files'
  one-item / folder rule with `free_name`. Tests use the Linux 7zz (26.01) when there's one.
- `pr7e/winshell.py` (Windows, ctypes COM): `thumbnail` (IShellItemImageFactory, THUMBNAILONLY), `show_menu`
  (IContextMenu from SHBindToParent + GetUIObjectOf; a hidden host window forwards INITMENUPOPUP etc. to
  IContextMenu2/3 so Send to fills; TrackPopupMenuEx at GetCursorPos), `menu_items` (tests). COM is STA per thread.
- `pr7e/rename.py` (no Qt): Files' batch rename (`from_template`, `find_replace`, `check` marks problems,
  `BatchRenameJob` renames through temporary names so swaps work). Preferences live in settings as `pref_*`
  (`main_window.PREFS` / `load_prefs`); "local" means not `places.is_remote` (UNC or DRIVE_REMOTE).
- `pr7e/remote.py` is PR7-F's engine, copied (keep them in step); `pr7e/servers.py` (no Qt): server folders are
  locations "sftp://user@host:22/path" (`split` / `join` / `key_for`); `CONNECTED` servers each keep one session
  under a lock (`Server.call` reconnects once); jobs (TransferJob up/down, DeleteJob, RenameJob, NewFolderJob,
  OpenJob) open their own sessions. Passwords only in keyring ("Paragon PR7-E"; PR7E_MEMORY_KEYRING in tests);
  known_hosts in PR7E_DATA. The window lists server folders with `_background` (worker thread; inline when
  `sync_jobs`), model mode "remote" (no stars, thumbnails, counts or search); server items travel on the clipboard /
  in drags as REMOTE_MIME. `servers.search` / `servers.tree_size` use `fs.search_filter` (the same rules as local
  search); `RelayJob` copies server -> server through a temporary folder. Tests run against tests/servers.py (PR7-F's
  real FTP / FTPS / SFTP servers).
- Windows shares (`places.share_path` / `share_root` / `share_label` / `connect_share` = WNetAddConnection2W,
  `shares()` in settings): UNC folders are plain local locations; the window's `open_share` asks to log in only when
  Windows can't open the folder. CI opens \\localhost\C$ for real.
- Expand folders (pref `expand_folders`, list view only): the model stays a flat list; `_with_children` puts each
  open folder's sorted contents after it with `Entry.depth`; `Tab.expanded` holds the open folders; `ListDelegate`
  paints the indent and arrow, `ListView.expand` asks to open / close. New Document: `places.templates()` (Windows'
  FOLDERID_Templates) and `ops.NewDocumentJob`; Set as Background: `places.set_wallpaper` (SPI_SETDESKWALLPAPER).
- Network: "network:" (in places.SPECIAL) lists `places.network_computers()` (winshell.network_items: Explorer's
  Network folder, FOLDERID_NetworkFolder, through IShellFolder) and a computer location "\\\\NAME"
  (`places.is_computer`) lists `computer_shares` (NetShareEnum level 1, disk shares, no $ ones); access denied ->
  login -> `connect_share(\\\\NAME\\IPC$)` -> again. Both via `_load_network` on a worker thread, model mode
  "network". Mouse side buttons: the window filters the whole application's mouse events; `side_button` trusts
  Qt's Back/ForwardButton, and on Windows also asks GetKeyState (Qt 6 reports some mice's side buttons as LEFT); a
  side click's press, moves and release are all swallowed (`_side_click`).
  Server stars keep `places.star_meta()`; server thumbnails / counts (pref "always" only) use `Server.aux`, a
  second session.
- Progress: `MainWindow._tick` (150 ms while anything runs or just finished) opens the operations popover by itself
  once a job has run OPEN_AFTER (0.4 s) unless closed by hand (`ops_closed`), drives the `progress` strip under the
  header bar, keeps finished jobs ("Finished") SHOW_FINISHED s; `done_note` puts the job title in the past tense for
  the corner note (`_toast` holds it 3 s over the selection text).
- 1.0: `winshell.show_background_menu` / `background_menu_items` (the folder's IShellFolder::CreateViewObject
  IContextMenu, Explorer's empty-space menu with New); `open_with_apps` / `open_with_app` (SHAssocEnumHandlers,
  IAssocHandler::Invoke on the item's IDataObject); `open_with_dialog` (SHOpenWithDialog with "always"). Properties'
  Permissions use `system.owner` (GetNamedSecurityInfoW + LookupAccountSidW), `set_readonly` (os.chmod),
  `set_hidden` (SetFileAttributesW).
- `pr7e/workers.py`: Asker (worker threads ask the GUI), JobRunner, Loader (thumbnails via QImageReader and folder
  counts on a QThreadPool, cached), Search (thread; results queued to the window).
- `pr7e/views.py`: EntryModel (plain Python list model, **never QStandardItemModel**: PR7-Z's crash), grid / list
  views with Files' drag and drop, Sidebar, PathBar, HeaderBar, TabStrip, SearchFilters, TrashBar, overlays.
- `pr7e/main_window.py`: `Tab` (location, history, own model + views, search) and `MainWindow`. Anything that asks
  goes through `_ask(kind, payload)`; tests set `window.answers[kind]` and `window.sync_jobs = True`. File keys
  (Delete, Ctrl+C, F2, Return...) are scoped to the views (`scope="files"`) so text fields keep them.
- `app.py`: `--selftest FOLDER OUT.json` opens a window offscreen and writes what it lists (CI runs it on the
  installed exe); `--check URL OUT.json` connects and lists (CI runs it on the installed exe against real FTPS and
  SFTP servers). Installer `installer/pr7e.iss` (Inno Setup 6, per user, AppId
  {6B8E2F14-3C5A-4D97-A1E0-7F29C4B85D36} permanent); CI installs it silently, runs `tools/verify_install.py
  installed` (CI installs with /MERGETASKS=defaultfolders), uninstalls, `... uninstalled`. The spec gives the .exe file details and no UPX; CI builds PyInstaller's
  bootloader from source and scans with Defender (PR7-F's false-positive fix).

## The look (family rules)

- Theme is PR7-F's (OBSIDIAN + AURORA via `theme.set_finish`; widgets with their own style sheets have `restyle()`).
  Colours: the family's only (hues ~318 -> 0 -> 42 deg, pearl and onyx greys); Inter; labels UPPER CASE; file
  names keep their case. Text on the selection band: `views.selected_text()` (white on OBSIDIAN, ink on AURORA).
- The PR7-E mark: PR7 `Logo.cpp` geometry plus an E (`widgets.E_SHAPE`); icon by `tools/make_icon.py`, wizard art
  by `tools/make_installer_art.py`. Check visuals with `QT_SCALE_FACTOR=2 python tools/screenshots.py OUT`.

## Rules

- Behaviour questions: do what GNOME Files does (menus, shortcuts, dialogs, wording), mapped to Windows.
- Tests: `python -m pytest` (offscreen) on real files in temp folders. Never the real settings (conftest), Recycle
  Bin (PR7E_TRASH) or, outside CI, the registry.
- Releases: bump `__version__` in `pr7e/__init__.py`, add a `## x.y.z` section to CHANGELOG.md; a `v*` tag publishes.
