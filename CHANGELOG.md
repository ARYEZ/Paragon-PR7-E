# Changelog

## 1.4.2

- The family list (`family.py`) knows the four newest Paragon apps: PR7-HX (hex editor), PR7-DB (SQLite browser),
  PR7-UN (uninstaller) and PR7-LN (launcher), so links between the apps and the PR7-U offer find them.

## 1.4.1

- The family's list knows the newest apps (PR7-HB, PR7-FS, PR7-AR, PR7-WM), so links between apps and Check for Updates find them.

## 1.4.0

- **Search with PR7-IS**: a folder's right-click menu, and the folder's empty space, start PR7-IS searching inside that folder (the folder is put in its search box as Everything's path word; type a name after it). Offered when PR7-IS is installed.

## 1.3.0

- **One finish for the whole family.** Choosing OBSIDIAN or AURORA here switches every Paragon app, open ones included (within a couple of seconds), and every app starts in the family's choice.
- In AURORA, group titles and selected rows are dark and readable (they were white on the light finish).

## 1.2.1

- Knows the whole family: PR7-DU, PR7-CB, PR7-IS, PR7-PM and PR7-SM are in its list of family apps (their permanent install IDs), so the family links find them when they're installed.

## 1.2.0

- **More family links** in the right-click menu: **Open in PR7-R** for PDFs (one tab each), **View in PR7-P** for a picture, **Play in PR7-M** for a song or video, when those apps are installed.
- **Check for Updates knows PR7-U**, the family installer. When it's installed, the update window has **Update All in PR7-U**, and a check by hand that finds PR7-E up to date offers to open PR7-U for the rest of the family.

## 1.1.0

- **Check for updates**: a few seconds after starting (at most once a day) PR7-E asks GitHub whether a newer release is out; if so, a note offers What's new, Install Now (downloads with progress, installs over this copy, and PR7-E reopens), Later or Skip This Version. "Main menu > Check for Updates" checks by hand. Only the app's name is sent.
- **Family links**: the right-click menu has Edit in PR7-W (text files), Edit Tags in PR7-L (music and folders) and Open in PR7-Z (archives) when those apps are installed.

## 1.0.0

PR7-E does everything planned from GNOME Files; this is its first permanent release.

- **Show More Options on empty space** (and Shift+F10 with nothing selected): the folder's own Windows menu, with
  Explorer's New submenu (Text Document, Shortcut, Office documents...).
- **Open With**: a submenu of the apps Windows recommends for the file's type (as in Explorer), and Open With Other
  Application… opens Windows' chooser with "Always use this app" (the only way Windows 10 / 11 let a default app be
  changed, on purpose).
- **Properties → Permissions**: the owner, your access (read and write / read only), and Explorer's Read-only and
  Hidden boxes, which change the file at once.

## 0.8.0

- **Progress you can see**, as in Files: the operations panel opens by itself for anything that isn't instant
  (copies, moves to another drive, uploads, compress, extract...), with each operation's bar, "x of y — n seconds
  left" and Cancel; a thin family-gradient bar runs across the window under the header while anything runs; a
  finished operation shows "Finished" for a moment, then the panel closes. Closing the panel keeps it closed until
  those operations end; a click elsewhere closes it.
- Every operation leaves a short note in the corner when it's done ("Moved “Elseworld” to “Backup”"), instant ones
  (a move on the same drive is a rename) included.

## 0.7.1

- Fixed: the mouse's back button acted like a left click. Qt 6 on Windows reports the side buttons of some mice as
  the left button; PR7-E now asks Windows which button is really down (GetKeyState: left up, XBUTTON1 / XBUTTON2
  down) and goes back / forward, swallowing the whole click so nothing is selected, opened or dragged.

## 0.7.0

- **The mouse's back and forward buttons** go back and forward anywhere in the window, as in Files (they were
  swallowed by the views).
- **Network** (Other Locations): the computers Windows sees on the network, as on Explorer's Network page; open one
  for its shared folders (logs in first when it asks, through Windows' IPC$ connection); the path bar has the
  computer as a step and Up goes from a share to its computer to Network.
- **Server folders**: stars (Starred shows them even while not connected), and with Show thumbnails / Count items
  set to Always, picture thumbnails and folder counts, fetched on a second connection so browsing never waits.

## 0.6.0

- **Expand folders in list view** (Preferences, off by default as in Files): folders get an arrow that opens them in
  place, indented; Right / Left open and close (Left inside goes to the folder's row); what's inside works like any
  item (select, copy, rename...) and changes inside show up. The grid keeps showing just the folder.
- **New Document** (empty space's menu, under New Folder): your Windows Templates folder (folders become submenus),
  Empty Document, and Open Templates Folder to add your own; the new file is selected and Rename opens. Undo trashes
  it.
- **Set as Background…** for a picture (Windows' own wallpaper setting).

## 0.5.0

- **Windows network shares**: type \\\\pc\\share (or Files' smb://pc/share) in Connect to Server or Ctrl+L. A share
  that needs a login asks for it and connects the way Map network drive does (Remember keeps the password in
  Windows Credential Manager). Shares you open are listed in the sidebar; eject takes them off and disconnects.
  They're ordinary Windows folders: copy, Trash (or delete when the share has none), search, thumbnails per the
  preference.
- **Search in server folders**, with When and What, on its own connection.
- **Properties for server items**: size (folders counted on the server), modified, permissions.
- **Copy between server folders** and between servers (down to a temporary folder, then up; Files' question on the
  way up); moving between servers deletes the originals after.
- Fixed: uploading to a server didn't refresh the folder shown.

## 0.4.0

- **Connect to Server** (Other Locations): sftp://, ftp:// (TLS when the server has it), ftps://, ftpes://, or just
  a host; recent servers; Files' login question (registered user or anonymous FTP; anonymous refused -> asks);
  "Remember password" keeps it in Windows Credential Manager only. Host keys and untrusted certificates are asked
  about, as Files does. PR7-F's engine.
- Server folders browse like any folder (grid / list, path bar, back / forward, tabs, Ctrl+L with an address);
  connected servers in the sidebar, eject to disconnect.
- Open a file on a server: downloaded to PR7-E's temporary folder and opened with its app.
- Copy / cut and paste, drag and drop, Download to…: between servers and folders both ways, with Files' Replace
  question and the progress pie; a move within one server is a rename.
- New folder, rename (with undo) and delete (for good, after asking: servers have no Trash) on servers.

## 0.3.0

- **Rename several files at once** (Rename… / F2 with more than one selected): Files' "Rename N Files" with a
  template ([Original file name], [1, 2, 3], [01, 02, 03], [001, 002, 003]; numbered by name or date) or find and
  replace; extensions kept; a preview marks names that can't be used (the same as another, taken, not allowed on
  Windows) and Rename waits until all are fine. Swaps work; one Undo puts every name back.
- **Preferences** (Ctrl+,): sort folders before files; open items with a single or double click; show Delete
  Permanently in the menu; search in subfolders; show thumbnails and count items in folders always, on this
  computer only (not network drives) or never.

## 0.2.0

- **Compress…**: Files' dialog (name; .zip, Encrypted .zip, .tar.xz, .7z). .zip and .tar.xz are made by Python;
  .7z and encrypted .zip (AES-256) by PR7-Z's 7-Zip engine (else an installed 7-Zip).
- **Extract Here / Extract to…** for everything 7-Zip reads (zip, 7z, rar, tar.*, iso, cab...); asks for the
  password when there is one. One item at the top comes out as itself, several go in a folder named after the
  archive; taken names get " (2)". Undo trashes what was made.
- **Show More Options** (and Shift+F10): the full Windows right-click menu, with other programs' items (PR7-Z,
  PR7-W, PR7-L, Send to, Defender...). Shift+right-click opens it straight away with Windows' extended items.
- **Thumbnails for videos, PDFs, Office files and HEIC** from Windows' own thumbnail handlers (what Explorer shows).

## 0.1.1

- **Use PR7-E for Folders** (main menu, and an installer option, off by default): double-clicking a folder or drive
  (on the desktop, in shortcuts, in other programs) and Win+E open PR7-E instead of File Explorer. Per user; another
  file manager's setting is kept and put back; unticking it or uninstalling PR7-E returns folders to File Explorer.
  The taskbar, Start and Windows' special places (This PC, Control Panel, the Recycle Bin's window) stay with
  Explorer.
- Fixed: "Open in PR7-E" on a drive opened the drive's current folder instead of its root (Explorer's "C:\\"
  arrives as C:" on the command line).
- The right-click item and the folders switch are separate: turning one off keeps the other.

## 0.1.0

First release: GNOME Files' window and behaviour in the Paragon look.

- Sidebar of places: Recent (Windows' recent items), Starred, Home and its folders (Windows' known folders, so a
  Documents moved to OneDrive is found), Trash, bookmarks (Ctrl+D), drives with eject, Other Locations.
- Header bar: back / forward, the path bar (Ctrl+L to type a path, the folder menu at its end), search, the grid /
  list switch, the operations key with its popover, the main menu.
- Grid view with picture thumbnails (made in the background, cached) and zoom; list view with SIZE, MODIFIED and a
  star column, sorted by clicking the headings; folder item counts; the floating selection label.
- Tabs (Ctrl+T, Ctrl+W, Ctrl+Page Up / Down), shown with two or more; each keeps its own history and selection.
- Search by typing: the folder and every folder in it, any case, accents ignored, with When and What filters and a
  LOCATION column; Open Item Location.
- Copy / cut / paste (Explorer's clipboard format too), drag and drop (same drive moves, another drive copies;
  Ctrl / Shift choose), Move to... / Copy to..., new folder, rename with Files' checks, "name (copy)" when pasting
  into the same folder, and Files' Replace / Merge / Skip / Rename question.
- Delete moves to the Windows Recycle Bin; Trash lists it with Restore and Empty; Shift+Delete deletes for good
  after asking; items that can't go to Trash ask to be deleted.
- Undo / Redo for copies, moves, renames, new folders, Trash and restores.
- Properties: size (folders counted in the background), type, parent folder, dates, free space.
- OBSIDIAN and AURORA finishes; Show Hidden Files (Ctrl+H); Keyboard Shortcuts.
- Installer (per user) with "Open in PR7-E" on Explorer's folder menu.
