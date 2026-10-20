# Petersburg API

Load the API with `$include "petersburg.su"`. Native functions are dispatched
inside the interpreter; scripts hold integer handles rather than C pointers.
All creation functions return a handle. Mutators return integer zero, getters
return their value, and invalid arguments/handles fail the USSR command with
a diagnostic.

| Function | Arguments | Result |
|---|---|---|
| `createwindow(title, width, height)` | String, positive integers (160..8192 by 120..8192) | Window handle |
| `maximize(window)` | Window handle | Zero; requests a maximized window |
| `label(window, text)` | Window handle, string | Label handle |
| `button(parent, text)` | Window or toolbar handle, string | Button handle |
| `toolbar(window)` | Window handle | Toolbar handle; docks at the top of the window, its buttons laid out left to right |
| `textbox(window, initial_text)` | Window handle, string | Text-box handle |
| `checkbox(window, text)` | Window handle, string | Checkbox handle |
| `listbox(window, items)` | Window handle, pipe-separated string | List handle |
| `combobox(window, items)` | Window handle, pipe-separated string | Combo-box handle |
| `image(window, path)` | Window handle, P6 PPM path | Image handle |
| `reloadimage(widget, path)` | Image handle, P6 PPM path | Zero; replaces the displayed image |
| `pack(widget, expand)` | Handle, boolean | Zero; expands vertically when true |
| `grid(widget, row, column, expand)` | Handle, non-negative row, column 0 or 1, boolean | Zero; responsive two-column placement |
| `settext(widget, text)` / `gettext(widget)` | Handle and optional string | Zero / current string |
| `getselection(widget)` | List or combo-box handle | Current selected item |
| `getindex(widget)` / `setindex(widget, index)` | List or combo-box handle and optional row index | Current / updated zero-based row |
| `setfocus(widget)` | Text-box or list handle | Zero; directs keyboard focus |
| `setchecked(widget, checked)` / `getchecked(widget)` | Checkbox or button handle and boolean | Zero / boolean; a checked button is drawn pressed (amber) to show an active tool or mode |
| `on(widget, event, callback_name)` | Handle and event name | Zero; `"click"`/`"change"` callbacks receive the widget handle; a window `"key"` callback receives the window handle and GLFW key code |
| `show(window)` / `close(window)` | Window handle | Zero |
| `destroy(widget)` | Widget or window handle | Zero |
| `run()` | No arguments | Zero; activates the scheduler-integrated UI |
| `messagebox(title, text)` | Two strings | Zero after the modal dialog closes |
| `canvas(window)` | Window handle | Canvas handle (retained vector drawing area, 240 px preferred height) |
| `canvasclear(canvas, color)` | Canvas, `0xRRGGBB` integer | Zero; drops every shape and sets the background |
| `canvasline(canvas, x1, y1, x2, y2, color)` | Canvas, four numbers, color | Zero; appends a line |
| `canvascircle(canvas, cx, cy, radius, color)` | Canvas, three numbers, color | Zero; appends a circle outline |
| `canvasarc(canvas, cx, cy, radius, start, end, color)` | Canvas, five numbers, color | Zero; appends an arc from `start` to `end` degrees |
| `canvasrect(canvas, x, y, width, height, color)` / `canvasfill(...)` | Canvas, four numbers, color | Zero; appends a rectangle outline / filled rectangle |
| `canvastext(canvas, x, y, text, color)` | Canvas, two numbers, string, color | Zero; appends bitmap-font text |
| `canvascount(canvas)` | Canvas | Number of retained shapes |
| `getwidth(widget)` / `getheight(widget)` | Handle | Laid-out size in framebuffer pixels; `0` until the window first renders |
| `setmark(list, index, marked)` / `getmark(list, index)` | List, row index, boolean | Zero / boolean; per-row highlight independent of the cursor |
| `getfocus(window)` | Window handle | Handle of the window's focused text box or list, or `0` |
| `alive(widget)` | Handle | `true` while the widget exists (and, for a window, is not closing) |
| `inject(widget, event, a, b, c)` | Handle, event name, three integers | Zero; feeds a synthetic event through the normal input path (for tests) |
| `uilock()` / `uiunlock()` / `uiidle()` | None | Serialize callback handlers that share state; `uiidle` is `true` when no handler holds the lock |

## Events

| Event | Widget | Callback parameters |
|---|---|---|
| `"click"` | Button, label, checkbox | widget |
| `"change"` | Text box, checkbox, list, combo box | widget |
| `"pointer"` | Image | image, x, y in source-image pixels |
| `"pointer"` | Canvas | canvas, x, y in canvas pixels |
| `"wheel"` | Canvas | canvas, x, y, direction (`1` away from the user, `-1` toward) |
| `"key"` | Window | window, GLFW key code (key press) |
| `"keyup"` | Window | window, GLFW key code (key release) |
| `"focus"` | Window | window, `1` when focused, `0` when focus is lost |

A callback's parameter count must match the table. Every callback runs as its
own scheduler process with a copy of the variables the main process had when
the event was delivered, so state shared between callbacks belongs in the
`!`/`?` hash namespace or in vectors and structs (which are shared by
reference). Wrap handlers that read-modify-write shared state in
`uilock(_):` / `uiunlock(_):`.

## Canvas

A canvas keeps a list of shapes and redraws it on every frame, clipped to its
rectangle. Coordinates are canvas-local framebuffer pixels with y growing
downward; they may be integers or reals. Colors are `0xRRGGBB` integers.
Arc angles are degrees, counter-clockwise as seen on screen, drawn from start
to end. Programs redraw by calling `canvasclear` and appending shapes again.
A canvas holds at most 200,000 shapes.

## Layout

Toolbars dock at the top of their window, one below another in creation
order, whatever the order other widgets were created in. A toolbar's
buttons are placed left to right at their text width and behave like any
other button (`"click"` callbacks, `settext`, `setchecked`, `inject`).

Widgets use a vertical flow by default; `grid` places widgets in up to two
responsive columns with row-based heights. Expanded grid rows share positive
remaining vertical space. Lists support Up/Down selection and scroll the
highlighted row into view. The current renderer uses OpenGL 2.1
compatibility drawing, with a built-in bitmap font; text input accepts ASCII.
Image widgets also support `"pointer"` callbacks receiving the image handle
and click coordinates in source-image pixels.

Click/change callbacks name a one-parameter USSR definition and receive the
widget handle. Image pointer callbacks name a three-parameter definition and
receive the image handle plus x/y source-image pixels. Window key callbacks
name a two-parameter definition and receive the window handle and GLFW key
code. Native GLFW callbacks only enqueue events; `scheduler.c` drains them
between VM instruction slices and schedules handlers through
`ussr_sched_spawn_func`.

## Image format

`image` currently loads binary P6 PPM data with a maximum component value of
255. Other image formats are rejected instead of silently ignored.
