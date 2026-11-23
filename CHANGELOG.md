# Changelog

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
