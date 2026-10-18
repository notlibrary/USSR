# Tver

Tver is a classic two-panel file manager built from Petersburg widgets. Run
`ussr src/tver/examples/hello.su` from the repository root with a GLFW-enabled
USSR build.

Click either file list or press **Tab** to activate a panel. Use **Up/Down** to
move the selection, **Enter** to open a directory, and **Insert** to mark or
unmark a file (the selection advances after marking). **F6** moves the
selected file, or all marked files, to the other panel. **F8** deletes the
selected file or marked files; press F8 a second time to confirm.

Moves do not overwrite an existing destination. F6 and F8 currently operate on
files only; directories can be entered but cannot be moved or deleted. There
is intentionally no file viewer or editor. Operations use native filesystem
calls, never a shell command. Errors and confirmations appear in the status
line.

The panels start in the interpreter's current directory and can navigate
through subdirectories and back to their parents. Directory entries appear
before files, case-insensitively sorted. The UI and native operations are
integrated with Petersburg's OpenGL renderer and USSR's existing scheduler.
