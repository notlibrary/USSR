# Kremlin

Kremlin is a lightweight full-screen desktop written in USSR on one
Petersburg canvas. Your desktop is an ordinary folder, `~/kremlin` (on
Windows `C:\Users\<name>\kremlin`): what you put there shows up as icons,
folders open on the desktop, files open with the program your system has for
them, and small `.kl` launcher files start programs. A panel along the bottom
holds the **KREMLIN** menu, pinned items, the latest message and a clock that
opens a calendar.

Start it from the repository root with a GLFW-enabled build:

    ussr src/kremlin/kremlin.su                 # full screen, ~/kremlin
    ussr src/kremlin/kremlin.su --window        # in a window
    ussr src/kremlin/kremlin.su D:/work         # another folder as the desktop
    ussr src/kremlin/kremlin.su --autostart on  # start it at every sign-in
    ussr src/kremlin/kremlin.su --autostart off

The first start creates the desktop folder with `Welcome.txt`, `Documents`
and `Apps`, which holds launchers for the terminal (the USSR REPL), Files
(Tver), the calculator (Klin), Drawing (Tula) and your home folder; the first
four are pinned to the panel. Kremlin finds USSR from `USSR_HOME`, or from the
folder it was started in when that is the repository (or `src/kremlin`), and
remembers it in the settings.

## Using it

- **Mouse.** Click selects, double-click opens, right-click shows what you
  can do with an icon, with the folder (on empty space) or with a pin. A
  single click on a pin starts it. The mouse wheel, or the arrow tabs at the
  edges, scroll when there are more icons than fit.
- **Keyboard.** Arrows, Home, End, Page Up and Page Down move the selection
  (icons run down the columns); Enter opens, Shift+Enter opens with the
  system program, Backspace (or Alt+Up) goes back up. F2 renames, Del
  deletes, Ctrl+D duplicates, Ctrl+P pins or unpins, Ctrl+N makes a folder,
  Ctrl+Shift+N a text file, Ctrl+L a launcher, Ctrl+R runs a command, F5
  refreshes, F1 lists the keys, Ctrl+Q exits. F10 or the Windows key opens
  the KREMLIN menu, the Menu key (or Shift+F10) the actions for the
  selection. Typing letters jumps to a name; in menus a letter jumps to an
  item.
- **Dialogs** take typed text (Backspace, Ctrl+Backspace or Ctrl+U clears),
  Enter for OK and Escape to cancel. A refused name keeps the dialog open
  and says why.
- **Deleting** asks first, deletes files, and deletes folders only when they
  are empty: nothing is ever removed recursively.
- **The KREMLIN menu** lists the launchers in `Apps`, goes back to the
  desktop, runs a command, and switches the wallpaper (Steppe, Kremlin,
  White nights, Birch, Graphite), the size (automatic by screen width, or
  1x to 3x) and starting at sign-in.
- **The clock** opens a month calendar (weeks start on Monday): arrows or
  the wheel change the month, Home or the bottom line return to today.

Other programs may change the folder: Kremlin looks again every three
seconds and keeps the selection.

## Launchers

A launcher is a text file ending in `.kl`:

    # Kremlin launcher
    name: Calculator
    command: "C:/USSR/ussr.exe" "C:/USSR/src/klin/klin.su"
    console: no

`command` is a program and its arguments (quote a program path with
blanks); a program next to the launcher can be named without a path. With
`console: no` a program starts without a console window on Windows (use it
for GUI scripts); a file of one plain line is just the command. Right-click a
launcher and choose **Edit command** to change it.

Opening a `.su` script runs it with USSR in a console; `.dxf` drawings open
in Tula; everything else opens with the system's program for it.

## Settings

`.kremlin` in the desktop folder is a `key=value` file Kremlin rewrites when
you change something from the menus: `scale` (0 automatic, 1-4), `wallpaper`
(0-4), `sort` (`name` or `type`), `pins` (paths from the desktop folder,
separated by `|`), `ussr` (the interpreter) and `source` (USSR's `src`
folder, used by the launchers it creates and by autostart).

## Starting with the system

`--autostart on` (or the menu item) writes
`%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup\kremlin.vbs` on
Windows, which starts Kremlin with `--detach`: that instance starts Kremlin
again without a console window and exits. On Linux it writes
`~/.config/autostart/kremlin.desktop`. `--autostart off` removes the file.

## Implementation

All of Kremlin is USSR code in `kremlin.su`. The engine (paths, the
listing, launchers, settings, layout, keyboard navigation, file operations,
dates) works without a window and is what `tests/kremlin_test.su` checks;
the GUI draws the wallpaper, icons, panel, menus, calendar and dialogs on one
canvas and turns clicks and keys into the same actions. It needed a few
additions underneath, all general: `launch` and `localtime` builtins,
Petersburg's `fullscreen`, `canvastextscale` and the canvas `"double"` and
`"menu"` (right-click) events. See `docs/API.md`.
