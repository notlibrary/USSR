# Tver

Tver is a classic two-panel file manager written in USSR on top of Petersburg
widgets. Start it from the repository root with a GLFW-enabled USSR build:

    ussr src/tver/tver.su            # panels open in the current directory
    ussr src/tver/tver.su some/dir   # panels open in some/dir

`src/tver/examples/hello.su` builds the same window from a script that
includes `tver.su`.

Click either file list or press **Tab** to activate a panel. Use **Up/Down** to
move the selection (hold them to keep moving), **Enter** to open a directory, and **Insert** to mark or
unmark a file or directory (the cursor advances after marking). Marked rows
are highlighted in amber, separately from the blue cursor. **F5** copies the
item under the cursor, or all marked items, to the other panel (directories
with everything in them); **F6** moves them. **F7** creates a directory: type
its name in the box that opens below the status line and press Enter (Esc
cancels). **F8** deletes the item or the marked items; press F8 a second time
to confirm.

Copies and moves do not overwrite an existing destination. F8 removes directories only
when they are empty and never deletes recursively. There
is intentionally no file viewer or editor. Operations use the standard
library's filesystem primitives, never a shell command. Errors and
confirmations appear in the status line.

The panels start in the interpreter's current directory and can navigate
through subdirectories and back to their parents. Directory entries appear
before files, case-insensitively sorted.

## Implementation

All of Tver is USSR code in `tver.su`; it has no native component of its own.
Directory access uses `list_dir`, `is_dir`, `getcwd`, `make_dir`,
`rename_path`, `copy_file`, `remove_file` and `remove_dir`, and the window is built from Petersburg widgets. Panel state is
kept in the `!`/`?` hash namespace because every Petersburg callback runs as a
separate scheduler process; key handlers are serialized with `uilock`.

## Tests

`tests/gui/tver_test.su` drives Tver with injected key events and checks the
listings and status messages:

    ussr tests/gui/tver_test.su

It needs a GLFW-enabled build and a display, creates `tver_fixture/` in the
current directory, and removes it again. `tests/gui/tver_repeat_test.su`
checks held-arrow navigation the same way, and
`tests/gui/tver_copy_mkdir_test.su` checks F5 and F7.
