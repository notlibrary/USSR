# tver test plan

- Package manifest parses as UNO (not JSON).
- Example includes `tver.su` and Petersburg API and runs on a GLFW-enabled build.
- Tab switches active panels; mouse clicks and Up/Down update selection.
- Enter opens directories and `..` returns to the parent.
- Insert toggles file marks, refuses directories, and preserves marks across
  panel refreshes.
- F6 moves files to the other panel and refuses to overwrite an existing file.
- F8 requires a second press and deletes files only; directories are refused.
- File names containing pipes and backslashes display and select correctly.
- Invalid handles/arguments report errors; filesystem errors reach the status line.
- Native filesystem operations run without invoking a shell.
- Verify Windows and POSIX builds and exercise both filesystem adapters.

## Project-specific

- Large directories, permission errors, symlink behavior, and Windows drive roots.
- Marked-file operations that encounter a failure after a partial success.
- Directory move/delete, copy, rename, view, and edit remain out of scope.
