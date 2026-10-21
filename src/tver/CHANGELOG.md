# Changelog

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
