# Changelog

## 0.6.0-alpha
Refine pass 1 from REFINE.md: the engine in `more.su` (no window needed),
the keys in `keys.su`.
- Keys: F2 and Shift+F6 rename in place; Shift+F4 makes an empty file; F3
  views a file (UTF-8, UTF-16, Latin-1, a hex dump, an image's size); F4
  edits it ($EDITOR, else Mbl); Enter opens files as Kremlin does (`.su`,
  `.dxf`, `.kl`, the system's program) and browses a `.zip` as folders;
  F5 inside a `.zip` extracts; Alt+F5 packs into a `.zip`; Shift+F8 moves
  to the trash (Kremlin's when there is one) after a confirmation with
  counts and sizes; F9 runs the user menu; Space sizes a folder; Ctrl+R
  refreshes; Ctrl+U swaps the panels; Ctrl+E brings the other panel
  here; Ctrl+L goes to a path; Ctrl+D and Ctrl+B are bookmarks; Alt+F1
  and Alt+F2 step through the drive roots; Alt+Left and Alt+Right are
  back and forward; Ctrl+H shows or hides dot files; Ctrl+F filters a
  folder; Ctrl+A, Num +, Num - and Num * mark; Alt+F7 finds files;
  Ctrl+O runs a command. A function key bar replaces the old key line.
- Engine: copy policies (overwrite, skip, rename, different), jobs with
  progress, cancel, a failure report and a queue; comparing and
  synchronising folders; ZIP reading (stored and deflated, with its own
  inflate), extracting and packing; recursive delete and the trash;
  shredding; find by name, size and text; multi-rename with counters,
  replace and case; SHA-256 and MD5 checksums with verification; split
  and join; a side-by-side diff; duplicates; disk usage; detail and
  brief views; sorting; settings in `~/.tver` (bookmarks, filters,
  hidden files, the user menu); a terminal view of both panels.
- `tests/tver_more_test.su` (112 checks), `tests/gui/tver_keys_test.su`
  (18 checks) and a ZIP fixture in `tests/fixtures/tver/`; the other Tver
  GUI tests are unchanged and pass.

## 0.5.0-alpha
- F5 copies the item under the cursor or the marked items to the other
  panel; directories are copied with their contents. Existing destinations
  and copying a directory into itself are refused.
- F7 creates a directory: a name box opens below the status line, Enter
  creates, Esc cancels. F8 stays delete.
- Added `tests/gui/tver_copy_mkdir_test.su`.

## 0.4.0-alpha
- Insert marks directories as well as files. Marks are drawn as Petersburg
  row marks (amber) instead of a `* ` prefix, survive navigation and
  refreshes, and are remembered per directory in each panel.
- F6 moves marked directories; F8 deletes empty directories and reports
  non-empty ones. Status messages count items.
- Added `tests/gui/tver_select_test.su`.

## 0.3.0-alpha
- Holding Up/Down repeats directory navigation (350 ms delay, 50 ms steps)
  and stops on key release, focus loss, window close, another key, or the
  end of the list. New callbacks: `tverhandlekeyup`, `tverfocus`.
- The left list has keyboard focus at start.
- Added `tests/gui/tver_repeat_test.su`.
- `ussr src/tver/tver.su [directory]` starts Tver directly.

## 0.2.0-alpha
- Rewrote Tver in USSR. `tver.c` is gone; `tver.su` uses the standard
  library's `list_dir`/`is_dir`/`getcwd`/`rename_path`/`remove_file` and
  Petersburg widgets. The `tverinitialize`, `tverhandlekey` and
  `tveractivatepanel` commands keep their arguments and behavior.
- Added an automated test, `tests/gui/tver_test.su`.

## 0.1.0-alpha
- Added a working two-panel file manager built with Petersburg widgets.
- Added directory navigation, keyboard panel switching, Insert marking, and
  confirmed F8 file deletion and non-overwriting F6 file moves.
- Added native cross-platform filesystem enumeration and scheduler key events.
