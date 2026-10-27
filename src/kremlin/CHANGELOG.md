# Changelog

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
