# Changelog

## 0.3.0
- `extra.su`: the Trash.
  - A bin in the bottom-right corner of the desktop, with papers in it
    when the Trash is full.
  - Delete moves items to `.Trash` (folders that are not empty too). A
    double click on the bin opens it, and its menu empties it.
  - Inside the Trash an item's menu restores it to where it was deleted
    from, or deletes it for good.
- Hover: the KREMLIN button lights up under the pointer, and the menu's
  items highlight as the pointer moves (Petersburg's canvas `"motion"`
  event).
- "Create shortcut on desktop" in an icon's menu puts a link to that folder
  or file on the desktop. "Add shortcut..." in the desktop's own menu makes
  one for any folder or program by its path. A launcher whose command is a folder's path opens it in
  Kremlin, and one whose command is a document's path opens the document
  as the desktop would.
- Holding an arrow key keeps moving the selection (key repeats reach the
  window now).

## 0.2.0
Refine pass 1 from REFINE.md; the new code is `more.su`.
- Trash: `kremlintrash` moves files and whole folders to `.trash` and
  remembers where they came from; `kremlintrashlist`, `kremlinrestore`,
  `kremlinemptytrash`.
- `kremlincopyto` and `kremlinmoveto` copy and move folders with
  everything in them, picking "name copy" beside a name already there.
- Clipboard: `kremlincut`, `kremlincopy`, `kremlinpaste`, and Ctrl+X/C/V
  in the window.
- `kremlinundo` (Ctrl+Z) takes back the last trash, move, rename, paste,
  copy, new folder or new file.
- Several at once: `kremlinselectmany`, `kremlinselection`,
  `kremlintrashselection`.
- `kremlinnewfromtemplate` from the `Templates` folder; `kremlininfo`
  (kind, size, lines, items, SHA-256); `kremlinfind` searches names and
  text under the desktop.
- Launchers take `icon`, `dir`, `env`, `hotkey`, `category` and `comment`
  (`km_launcher_info`); `dir` and `env` apply when the launcher starts.
- Settings: `assoc.EXT = program` or a `.kl` launcher (file
  associations), `showhidden`, `clock = 24h|12h|24s`, `lang = en|ru`
  (menu labels), `kiosk` (no Exit), and `kremlinexport`/`kremlinimport`.
- Menus: sub-folders of `Apps` are categories in the KREMLIN menu;
  `kremlinsearch` (fuzzy, launchers and recent files), `kremlinrecent`,
  `kremlinopenwith` and `kremlinopenwithrun`, and the Run dialog keeps
  `.runhistory` (`kremlinrunhistory`).
- Calendar: notes in `.calendar` (`kremlinnote`, `kremlinnotes`), ISO
  week numbers (`km_weeknum`), holidays from `.holidays`, and `.ics`
  import (`kremlinimportics`).
- `kremlinnotify` queues messages; `kremlinnotifications` lists them.
- `tests/kremlin_more_test.su`: 82 checks. `tests/kremlin_test.su` names
  its own tree remover `kt_remove_tree` (the library's is now included);
  the GUI test also removes `.runhistory`.

## 0.1.0
- First version: a full-screen desktop on one canvas showing the folder
  `~/kremlin` (`C:\Users\<name>\kremlin`) as icons, with a panel holding the
  KREMLIN menu, pinned items, messages and a clock with a month calendar.
- Folders open on the desktop with a location bar and Back; files open with
  the system's program, `.su` scripts run with USSR, `.dxf` drawings open in
  Tula, `.kl` launchers start their command (optionally without a console).
- Right-click menus for icons, the folder and pins; dialogs for new folders,
  files and launchers, rename, edit command, run, delete (empty folders
  only), properties and exit.
- Full keyboard control: arrows, Home/End/PgUp/PgDn, Enter, Backspace, F2,
  Del, Ctrl+D/P/N/L/R/Q, F5, F10 or the Windows key, the Menu key, typing to
  jump to a name.
- First run creates Welcome.txt, Documents and Apps with launchers for the
  terminal, Files (Tver), the calculator (Klin), Drawing (Tula) and the home
  folder. Settings in `.kremlin`: size, wallpaper, sort, pins, USSR paths.
- `--autostart on|off` starts Kremlin at sign-in (Windows Startup folder,
  `~/.config/autostart`), `--detach` restarts it without a console window.
- Added `tests/kremlin_test.su` (engine) and `tests/gui/kremlin_test.su`
  (GUI).
