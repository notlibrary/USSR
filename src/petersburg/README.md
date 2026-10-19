# Petersburg (`petersburg.su`)

Petersburg is USSR's cross-platform widget library. Its public API is written
in `.su`; the interpreter's C runtime owns window/widget handles, input, layout,
and an OpenGL renderer. GLFW supplies the portable window and OpenGL-context
layer on Windows and Linux.

## Build support

Petersburg's C module is part of the normal interpreter build. Autotools
enables the native OpenGL backend by default and requires GLFW and OpenGL
development files; use `./configure --disable-petersburg` only for a
deliberately headless build. Release binaries bundle the GLFW runtime library
alongside the executable, so users do not need to install GLFW separately.

- Autotools: install GLFW 3 development files and OpenGL development files,
  then run `./configure --enable-petersburg` and `make`.
- GNU make: install GLFW 3 and OpenGL development files discoverable through
  `pkg-config`, then run `make -f ussr-ubuntu.mk`.
- MSVC: install GLFW 3, then pass `GLFW_DIR` and `GLFW_LIB` to NMAKE, for
  example `nmake /f Makefile.msc GLFW_DIR=C:\glfw GLFW_LIB=C:\glfw\lib-vc2022\glfw3.lib`.

## Quick start

From the repository root, run `ussr src/petersburg/examples/hello.su` with the
USSR interpreter. The script creates a window, adds controls, registers a named
callback, and calls `run(_)` to hand event pumping to the existing scheduler. A
callback definition receives the widget handle as its single argument and
executes as a normal scheduled USSR process.

## Widgets and layout

`createwindow`, `label`, `button`, `textbox`, `checkbox`, `listbox`,
`combobox`, and `image` return opaque integer handles. Each widget is
automatically packed into its window in creation order. Its default height is
based on its type; widths track the window. `pack(widget, expand)` marks a
widget to share additional vertical space when the window grows. `grid(widget, row, column, expand)` lays widgets out in
responsive rows and two columns; expanded rows take remaining window height.

Lists and combo boxes take a pipe-separated string, for example
`"ONE|TWO|THREE"`. Images currently accept binary P6 PPM files. Text rendering
uses a small built-in bitmap font; text input is ASCII.

## Events and lifecycle

`on(widget, "click", "handler")` and `on(widget, "change", "handler")`
register a one-argument USSR definition. Button clicks fire `click`; checkbox,
text-box, list, and combo-box changes fire `change`. A window can register
`on(window, "key", "handler")`; its callback receives the window handle and
the GLFW key code as two arguments. List widgets support Up/Down selection and
scrolling. The native layer only queues input events; the scheduler starts the
handlers outside OpenGL/GLFW callbacks.

Call `run(_)` after setup. The scheduler continues processing scripts while
the UI is open and returns when all Petersburg windows close. Use `close` to
request a window close, and `destroy` to release a widget or its entire window
and children. `messagebox(title, text)` displays a modal OpenGL dialog.

See `docs/API.md` for argument and return contracts.
