# Kremlin — refinement ideas

**Today:** a full-screen desktop on one Petersburg canvas that shows
`~/kremlin` as icons. `.kl` launchers start programs. A panel holds the
KREMLIN menu, pins, messages and a clock with a month calendar. It has
right-click menus, dialogs (new folder/file/launcher, rename, run,
delete, properties), full keyboard control, type-to-find, five
wallpapers, scale 1x–3x, autostart at sign-in, and Alt+Tab under Moscow.
It looks at the folder again every three seconds.

**Refine pass 1 (0.2.0):** `more.su`; [x] done, [~] partly done. See
CHANGELOG.md and `tests/kremlin_more_test.su`.

## Desktop and icons
- Free placement: drag icons and remember their positions in `.kremlin`
- [~] Rubber-band (lasso) selection and Ctrl/Shift multi-select, with
  operations on all selected icons — partly: kremlinselectmany, kremlinselection and kremlintrashselection; no lasso or Ctrl/Shift clicks yet
- Drag and drop between folders, onto pins, and onto launchers (open
  this file with that program)
- Thumbnails for images through Omsk, and first lines of text files as
  previews
- [~] Icons per file type from a theme folder, and custom icons for
  launchers (`icon:` key) — partly: launchers keep an icon: key (km_launcher_info); the canvas does not draw it yet
- [~] Show or hide hidden files, sort by date or size, list and details
  views — partly: showhidden = yes in .kremlin shows dot files (not .trash); no date or size sort or other views
- [x] A trash folder in place of permanent deletion, with restore
- [~] Recursive copy and move with progress, reusing Tver's code — partly: kremlincopyto and kremlinmoveto copy and move folders with everything in them; no progress
- [x] Cut, copy and paste of files (Ctrl+X/C/V)
- [x] Undo the last file operation

## Panel
- A task list: buttons for running programs and windows, click to focus
- A system tray area: network status (Murmansk drops), volume, battery
- A panel at the top, left or right, with auto-hide
- [x] A clock format setting (24h/12h, seconds, date)
- [x] Calendar notes: click a day to add a note kept in
  `~/kremlin/.calendar`
- [~] Notifications: a message queue with history, and an API other
  programs can post to — partly: kremlinnotify queues messages and shows them; kremlinnotifications lists them; no history pane

## Menus and launching
- [~] Searchable start menu: type to filter launchers and recent files — partly: kremlinsearch filters launchers and recent files fuzzily; the menu does not take typing yet
- [x] A recent documents list
- [x] Categories in the KREMLIN menu from sub-folders of `Apps`
- A `.desktop` file importer on Linux and a `.lnk` reader on Windows
- [x] File associations editable in settings (`assoc: .dxf = Tula`)
- [x] "Open with..." listing every launcher
- [~] Run dialog history and completion — partly: km_run keeps .runhistory (kremlinrunhistory); no completion in the dialog

## Window management (Moscow)
- Virtual desktops (workspaces) with Ctrl+Alt+arrows
- Window snapping to halves and quarters, and a tiling mode
- Minimise, maximise and close from the task list
- An Alt+Tab switcher with window previews

## Look and feel
- Wallpapers from image files (PNG/JPEG via Omsk decoding)
- Light and dark themes, an accent colour, Pobeda or a system font for
  labels
- Wallpaper slideshow and a solid colour option
- A screen lock with a clock, and a screensaver
- Accessibility: a large-text theme, high contrast, keyboard-only mode
  hints

## Settings
- A settings window (Petersburg widgets) in place of hand-editing
  `.kremlin`
- Several desktop profiles
- [x] Import and export of settings and pins

## Integration
- A file-system watcher (inotify, ReadDirectoryChangesW) in place of
  polling every three seconds
- Desktop widgets: a KGB fact of the day, a Novgorod feed, a Sochi
  table finder
- Session save and restore: reopen the programs that were running
- A screenshot key (PrtSc) that saves PNG into `~/kremlin/Pictures`

## Tests
- Snapshot tests of the rendered canvas
- Stress tests with folders of 5 000 files

## Launchers and files
- [x] Launcher keys beyond `name`, `command` and `console`: `icon`, `dir`
  (working directory), `env`, `hotkey`, `category`, `comment`
- Global hotkeys from launchers (Ctrl+Alt+T for the terminal)
- [x] Templates in a `Templates` folder: New > from template
- Rename in place on the icon label, in addition to the dialog
- Recursive folder delete with a count confirmation (today only empty
  folders are removed)
- [~] Properties with size on disk, dates, permissions and a checksum — partly: kremlininfo gives kind, size, lines, items and SHA-256; no dates or permissions
- Mounted drives and removable media as desktop icons, with eject
- Network places: an FTP/HTTP folder shown through Ryazan

## Search and information
- [x] Desktop search: file names and text contents under `~/kremlin`
- A system monitor panel item: CPU, memory, scheduler tasks, and a
  process list with kill
- A clipboard history panel item
- World clocks and a timer or alarm in the calendar popup
- [x] Calendar: week numbers, holidays from a file, and `.ics` import
- A weather or currency widget over Ryazan HTTPS (opt-in)

## Sessions and users
- A sign-in screen and several users under Moscow
- Shut down, restart, sleep and sign out in the KREMLIN menu on Moscow
- First-run tour that points at the menu, pins and F1
- [~] Kiosk mode: one program, no menus, for a public terminal — partly: kiosk = yes drops Exit Kremlin from the menu; the menus stay
- [~] Localisation of every label (Russian and English), with the date
  format following it — partly: km_tr and lang = ru translate the menu labels; the rest and the date format do not follow yet
- A crash guard: if drawing fails, restart the desktop with the session
  kept
