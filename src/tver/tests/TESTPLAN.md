# tver test plan

Automated: `ussr tests/gui/tver_test.su` (GLFW build and a display required)
covers listing order, directory mark refusal, file marks and cursor advance,
Tab, Enter into a directory and back to `..`, F6 moves with marks, and the
two-step F8 delete. `ussr tests/gui/tver_repeat_test.su` covers held-arrow
repeat: single tap, hold, release, focus loss, end of list, direction
change, cancellation by another key, an unfocused list, and closing the
window mid-repeat. `ussr tests/gui/tver_select_test.su` covers marking files
and directories, `..` refusal, cursor versus mark, marks across navigation,
refresh, entering and leaving directories, per-panel marks, F6 on marked
directories, and F8 on empty and non-empty directories.
`ussr tests/gui/tver_copy_mkdir_test.su` covers F5 on the cursor row, on
marked files and directories (nested contents), refusal of an existing
destination and of copying a directory into itself, and F7: creating a
directory, cursor placement, Esc, keys ignored while the prompt is open,
bad and existing names, and an empty directory.

Manual:

- Package manifest parses as UNO (not JSON).
- Example includes `tver.su` and Petersburg API and runs on a GLFW-enabled build.
- Mouse clicks and Up/Down update selection and activate the clicked panel.
- F5 and F6 refuse to overwrite an existing file.
- Typing a name into the F7 box with the keyboard (the test sets its text).
- File names containing pipes and backslashes display and select correctly.
- Filesystem errors (unreadable directories, locked files) reach the status line.
- Verify Windows and POSIX builds (drive roots, UNC paths, `/`).

## Project-specific

- Large directories, permission errors, symlink behavior, and Windows drive roots.
- Marked-file operations that encounter a failure after a partial success.
- Rename, view, edit and recursive delete remain out of scope.
