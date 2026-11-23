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
  (AssocQueryStringW), terminal. `pr7e/shell.py`: "Open in PR7-E" registry verbs (HKCU, Directory / Drive /
  Background); the installer calls `--register-menu` / `--unregister`.
- `pr7e/workers.py`: Asker (worker threads ask the GUI), JobRunner, Loader (thumbnails via QImageReader and folder
  counts on a QThreadPool, cached), Search (thread; results queued to the window).
- `pr7e/views.py`: EntryModel (plain Python list model, **never QStandardItemModel**: PR7-Z's crash), grid / list
  views with Files' drag and drop, Sidebar, PathBar, HeaderBar, TabStrip, SearchFilters, TrashBar, overlays.
- `pr7e/main_window.py`: `Tab` (location, history, own model + views, search) and `MainWindow`. Anything that asks
  goes through `_ask(kind, payload)`; tests set `window.answers[kind]` and `window.sync_jobs = True`. File keys
  (Delete, Ctrl+C, F2, Return...) are scoped to the views (`scope="files"`) so text fields keep them.
- `app.py`: `--selftest FOLDER OUT.json` opens a window offscreen and writes what it lists (CI runs it on the
  installed exe). Installer `installer/pr7e.iss` (Inno Setup 6, per user, AppId
  {6B8E2F14-3C5A-4D97-A1E0-7F29C4B85D36} permanent); CI installs it silently, runs `tools/verify_install.py
  installed`, uninstalls, `... uninstalled`. The spec gives the .exe file details and no UPX; CI builds PyInstaller's
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
