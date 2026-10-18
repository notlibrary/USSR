# Tver API

The example in `examples/hello.su` demonstrates the supported application
surface. It includes `tver.su`, creates six Petersburg widgets, lays them out
with `grid`, registers key and panel-change callbacks, and initializes the
native filesystem state.

## Native commands

| Command | Arguments | Behavior |
|---|---|---|
| `tver_initialize` | Window, left path label, left list, right path label, right list, status label | Starts both panels in the current directory |
| `tver_handle_key` | Window handle, GLFW key code | Handles Tab, Insert, Enter, F6, and F8 |
| `tver_activate_panel` | Changed list handle | Makes the clicked list the active panel |

UI input is delivered from GLFW through Petersburg's event queue and run as a
normal USSR scheduler callback. `tver.su` provides script-level wrapper
definitions for these native commands.

## Key bindings and safety

- Tab switches the active panel. Clicking a list also activates it.
- Up/Down changes the selected row; Enter enters a selected directory.
- Insert toggles a file mark and advances to the next row; directories cannot
  be marked.
- F6 moves the selected file, or all marked files, into the other panel.
- F8 requires a second press to confirm deletion of the selected or marked
  files.
- Moves refuse to overwrite existing destination files.
- Directory move/delete, copy, rename, view, edit, and recursive deletion are
  not implemented in this initial usable version.
