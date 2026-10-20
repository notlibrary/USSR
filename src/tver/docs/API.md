# Tver API

The example in `examples/hello.su` demonstrates the supported application
surface. It includes `tver.su`, creates six Petersburg widgets, lays them out
with `grid`, registers key and panel-change callbacks, and initializes the
panels.

## Commands

| Command | Arguments | Behavior |
|---|---|---|
| `tverinitialize(r)` | Window, left path label, left list, right path label, right list, status label | Starts both panels in the current directory; `r` is `0`, or `1` if a panel could not be read |
| `tverhandlekey(r)` | Window handle, GLFW key code | Handles Tab, Insert, Enter, keypad Enter, F6, and F8 |
| `tveractivatepanel(r)` | Changed list handle | Makes the clicked list the active panel |
| `tverhandlekeyup(r)` | Window handle, GLFW key code | Window `"keyup"` callback: releasing a held arrow stops repeating |
| `tverfocus(r)` | Window handle, `1`/`0` | Window `"focus"` callback: losing focus stops repeating |

All of these are USSR definitions in `tver.su`. UI input is delivered from GLFW
through Petersburg's event queue and runs as a normal USSR scheduler callback.

## Running and including

`ussr src/tver/tver.su [directory]` starts the application: its `init` is
wrapped in `$ifdef __MAIN__`, so it exists only when `tver.su` is the file
`ussr` was started with. Scripts that `$include` it keep their own `init`
and call the commands above. A directory argument that does not exist, or
more than one argument, prints a message and exits with a non-zero status.

## Key bindings and safety

- Tab switches the active panel. Clicking a list also activates it.
- Up/Down changes the selected row; Enter enters a selected directory.
- Holding Up or Down repeats the movement: after 350 ms the cursor steps
  every 50 ms until the key is released, the window loses focus or closes,
  another key is pressed, or the cursor reaches the end of the list. A tap
  still moves exactly one row. Register the `"keyup"` and `"focus"`
  callbacks shown in the example; without them a repeat runs until the end
  of the list or the next key press.
- Insert toggles a file mark and advances to the next row; directories cannot
  be marked.
- F6 moves the selected file, or all marked files, into the other panel.
- F8 requires a second press to confirm deletion of the selected or marked
  files. The confirmation is tied to the panel, directory, cursor row and mark
  count; changing any of them disarms it.
- Moves refuse to overwrite existing destinations.
- Entering another directory clears that panel's marks.
- Directory move/delete, copy, rename, view, edit, and recursive deletion are
  not implemented.
