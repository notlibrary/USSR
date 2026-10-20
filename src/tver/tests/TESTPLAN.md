# tver test plan

Automated: `ussr tests/gui/tver_test.su` (GLFW build and a display required)
covers listing order, directory mark refusal, file marks and cursor advance,
Tab, Enter into a directory and back to `..`, F6 moves with marks, and the
two-step F8 delete.

Manual:

- Package manifest parses as UNO (not JSON).
- Example includes `tver.su` and Petersburg API and runs on a GLFW-enabled build.
- Mouse clicks and Up/Down update selection and activate the clicked panel.
- F6 refuses to overwrite an existing file.
- F8 deletes files only; directories are refused.
- File names containing pipes and backslashes display and select correctly.
- Filesystem errors (unreadable directories, locked files) reach the status line.
- Verify Windows and POSIX builds (drive roots, UNC paths, `/`).

## Project-specific

- Large directories, permission errors, symlink behavior, and Windows drive roots.
- Marked-file operations that encounter a failure after a partial success.
- Directory move/delete, copy, rename, view, and edit remain out of scope.
