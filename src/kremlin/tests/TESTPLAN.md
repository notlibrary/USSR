# Kremlin test plan

Automated:

- `ussr tests/kremlin_test.su` (any build, also run through russia.su by
  `tests/run_all.sh`): paths (drives, `/`, parents, extensions), kinds and
  labels, hidden names, label wrapping, dates and the calendar page,
  `localtime`, launcher files and command lines, the settings file, typed
  characters, the grid layout, hit testing, arrow navigation and scrolling;
  then a scratch desktop in `tests/kremlin_tmp`: the first-run contents and
  pins, new folders/files/launchers with bad and taken names, duplicate,
  rename (launchers keep `.kl`, pins follow), pins saved, opening folders and
  Back, what opening a script, document, launcher or missing file does
  (`km_dryrun`), Run, type-to-find, sorting by type, delete (full folders
  refused, unpinning), the autostart file, settings kept across a restart.
- `ussr tests/gui/kremlin_test.su` (GLFW build and a display): the desktop
  and panel are drawn; click selects and clears, double-click opens a folder,
  Back; right-click menus on space and icons; typing in the New folder and
  Rename dialogs, a refused name keeping the dialog, the OK button; Del with
  its confirmation; the KREMLIN menu starting an app; F10 and a letter in a
  menu; Exit asking first; type-to-find, Home/Down; pins starting their
  launcher; Ctrl+R Run; Ctrl+P adding a pin to the panel; the calendar and
  its month arrows; F1 help.

Manual:

- `ussr src/kremlin/kremlin.su` covers the whole screen with no border;
  other programs started from it open on top; Alt+Tab returns to it.
- At 1920 pixels or wider the interface doubles in size; the KREMLIN menu's
  Size item cycles 1x-3x and automatic.
- Start Calculator, Files and Drawing from the panel: no console windows.
  Start Terminal: a console with the USSR REPL.
- Create a file in the desktop folder from another program: its icon shows
  up within three seconds.
- `--autostart on`, sign out and in: Kremlin starts without a console;
  `--autostart off` removes it.
