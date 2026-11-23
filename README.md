# Paragon PR7-E

A file manager for Windows that works the way GNOME Files (Ubuntu's file manager) works, in the Paragon family look.

- Sidebar: Recent, Starred, Home and its folders, Trash (the Windows Recycle Bin), bookmarks, drives, Other Locations
- Header bar: back / forward, the path bar (click a folder; Ctrl+L to type a path), search, grid / list, main menu
- Grid view with picture thumbnails, list view with a star column; zoom; tabs (Ctrl+T)
- Type to search: the folder and every folder in it, with When / What filters
- Copy, cut, paste (with Explorer too), drag and drop, Move to / Copy to, rename (F2), new folder,
  Delete to Trash, Shift+Delete for good, Undo / Redo, and the copy / move progress in the header bar
- "Open in PR7-E" on Explorer's folder menu (an installer option)

Install: `PR7-E-Setup-x.y.z.exe` from the build's artifacts or a release.
Run from source: `pip install -r requirements.txt` then `python -m pr7e [folder]`. Tests: `python -m pytest`.
