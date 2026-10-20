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

All three are USSR definitions in `tver.su`. UI input is delivered from GLFW
through Petersburg's event queue and runs as a normal USSR scheduler callback.

## Key bindings and safety

- Tab switches the active panel. Clicking a list also activates it.
- Up/Down changes the selected row; Enter enters a selected directory.
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
