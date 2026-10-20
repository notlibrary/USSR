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
- Insert toggles the mark on the file or directory under the cursor and
  advances to the next row; `..` is never marked.
- F6 moves the item under the cursor, or all marked items, into the other
  panel (directories included, same volume).
- F8 requires a second press to confirm deletion of the item under the
  cursor or the marked items. Files are deleted; directories only when empty
  (a non-empty one is reported and stays marked). The confirmation is tied to
  the panel, directory, cursor row and mark count; changing any of them
  disarms it.
- Moves refuse to overwrite existing destinations.
- Copy, rename, view, edit, and recursive deletion are not implemented.

## Selection

- Marks are Petersburg list row marks: an amber band, bar and text. The
  cursor is the blue band (brighter while the list has keyboard focus), so a
  marked row under the cursor shows both.
- Marks survive cursor movement, held-arrow repeat, scrolling and list
  refreshes (marking, moves and deletes in the same directory).
- Each panel remembers marks per directory. Entering a directory shows that
  directory's own marks (none the first time); going back restores the
  marks you left. The two panels keep separate marks.
- F6 and F8 use the marks of the current directory only. Items that are moved
  or deleted lose their marks; items that fail keep them.
- The status line reports `N item(s) marked in this panel` after each
  Insert.
