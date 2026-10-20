# Tver

Tver is a classic two-panel file manager written in USSR on top of Petersburg
widgets. Start it from the repository root with a GLFW-enabled USSR build:

    ussr src/tver/tver.su            # panels open in the current directory
    ussr src/tver/tver.su some/dir   # panels open in some/dir

`src/tver/examples/hello.su` builds the same window from a script that
includes `tver.su`.

Click either file list or press **Tab** to activate a panel. Use **Up/Down** to
move the selection (hold them to keep moving), **Enter** to open a directory, and **Insert** to mark or
unmark a file (the selection advances after marking). **F6** moves the
selected file, or all marked files, to the other panel. **F8** deletes the
selected file or marked files; press F8 a second time to confirm.

Moves do not overwrite an existing destination. F6 and F8 currently operate on
files only; directories can be entered but cannot be moved or deleted. There
is intentionally no file viewer or editor. Operations use the standard
library's filesystem primitives, never a shell command. Errors and
confirmations appear in the status line.

The panels start in the interpreter's current directory and can navigate
through subdirectories and back to their parents. Directory entries appear
before files, case-insensitively sorted.

## Implementation

All of Tver is USSR code in `tver.su`; it has no native component of its own.
Directory access uses `list_dir`, `is_dir`, `getcwd`, `rename_path` and
`remove_file`, and the window is built from Petersburg widgets. Panel state is
kept in the `!`/`?` hash namespace because every Petersburg callback runs as a
separate scheduler process; key handlers are serialized with `uilock`.

## Tests

`tests/gui/tver_test.su` drives Tver with injected key events and checks the
listings and status messages:

    ussr tests/gui/tver_test.su

It needs a GLFW-enabled build and a display, creates `tver_fixture/` in the
current directory, and removes it again. `tests/gui/tver_repeat_test.su`
checks held-arrow navigation the same way.
