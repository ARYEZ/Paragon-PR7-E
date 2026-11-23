# Changelog

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
