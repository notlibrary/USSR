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
| `fullscreen(window)` | Window handle | Zero; makes the window a borderless window covering the primary monitor, with widgets edge to edge (no margin); `1` when there is no monitor and it was maximized instead |
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
| `grid(widget, row, column, expand)` | Handle, non-negative row, column 0, 1 or 2 (both columns), boolean | Zero; responsive two-column placement |
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
| `canvastextscale(canvas, x, y, text, color, scale)` | As `canvastext`, then an integer 1..16 | Zero; bitmap-font text with each font pixel `scale` pixels square (a character cell is `7 * scale` wide) |
| `canvasfonttext(canvas, x, y, text, color, font, size)` | Canvas, two numbers, UTF-8 string, color, font handle, pixels per em 1..1000 | Zero; anti-aliased outline-font text whose line box starts at `(x, y)` (the baseline is the font's ascent below `y`) |
| `fontinfo(path)` | Font file path | `"Family|Style"` (name IDs 1 and 2, English when present), or `""` when the file is not a TrueType-outline font (`.ttf`, first face of a `.ttc`) |
| `loadfont(path)` | Font file path | Font handle `1..64`, the same for the same path, or `0` when it cannot be used |
| `fontwidth(font, size, text)` / `fontheight(font, size)` | Font handle, pixels per em, and text | Advance width of the text (a real) / line height (ascent + descent + line gap, an integer) in pixels; sizes are rounded to whole pixels as drawing does |
| `canvascount(canvas)` | Canvas | Number of retained shapes |
| `canvascursor(canvas, mode)` | Canvas, `"hidden"` or `"normal"` | Zero; `"hidden"` hides the system cursor while it is over the canvas, for programs that draw their own |
| `setmodal(window, on)` | Window handle, boolean | Zero; while a modal window is open the other windows take no input, and a click on one brings the modal window to the front |
| `buttonicon(button, picture)` | Button handle, `"folder"`, `"disk"`, `"pointer"` or a P6 PPM path | Zero; draws the picture in toolbar buttons; magenta (255, 0, 255) in a PPM is transparent |
| `buttonhint(button, text)` | Button handle, tooltip text | Zero; shows the text while the pointer is held over the button |
| `setvisible(toolbar, visible)` | Toolbar handle, boolean | Zero; shows or hides the toolbar |
| `setmovable(toolbar, movable)` | Toolbar handle, boolean | Zero; enables or disables dragging to reorder it |
| `toolmenu(toolbar, label, items)` | Toolbar handle, a label, `"item\|item\|-\|item"` (`-` draws a line) | The menu's handle. A click on it drops the items down; picking one (click, or Up/Down and Enter) calls its `"select"` callback with the menu and the item's index; Escape or a click elsewhere closes it |
| `setitems(menu, items)` | Menu handle, items as for `toolmenu` | Zero; replaces the menu's items |
| `recentfiles(list): app` / `recentadd(list): app path` | A program name, a path | The program's recent files, newest first (10 kept, in `~/.<app>-recent`). `$include "recent.su"` |
| `filemenuitems(items): list` / `filemenuaction(pick): index list` | A recent list, a picked index | The items of a standard File menu (New, Open..., Save, Save As..., the last 5 files, Exit), and `[action path]` for a picked item: `"new"`, `"open"`, `"save"`, `"saveas"`, `"recent"` with its path, `"exit"`, or `""` |
| `filedialog(path): mode title start filters` | `"open"` or `"save"`, a title, a folder or file to start at, `"label\|patterns\|..."` filters (patterns split by `;`) | The chosen path, or `""` when cancelled; modal, it returns once answered. It opens focused on the list's first row; a double click on a row opens it (a folder too). `$include "filedialog.su"` |
| `canvaskeep(canvas)` / `canvasback(canvas)` | Canvas | The shape count; `canvaskeep` marks the shapes drawn so far, `canvasback` drops every shape added after the mark (`canvasclear` drops the mark), so a moving overlay is redrawn alone |
| `canvasdash(canvas, x1, y1, x2, y2, color, dash, gap)` / `canvasdashcircle(canvas, cx, cy, radius, color, dash, gap)` | Canvas, numbers, color, dash and gap lengths in pixels | Zero; a dashed line (line shapes) or a dashed circle (arcs), cut natively |
| `canvaspen(canvas, width)` | Canvas, width in pixels (0 to 64) | Zero; the lines, circles and arcs drawn after it are that wide (0 or 1: thin), thick lines with round ends; `canvasclear` sets it back to thin |
| `psb_draw(_): canvas x y length vertical first visible total` and `psb_hit`, `psb_thumb`, `psb_first_at` | A canvas strip and a position (the first unit in view, how many are visible, how many there are) | A scrollbar drawn on a canvas (`PSB_THICK` pixels), whether a press is before (-1), on (0) or after (1) its thumb, the thumb's `[start size]`, and the first unit for a dragged thumb. `$include "scrollbar.su"` |
| `getwidth(widget)` / `getheight(widget)` | Handle | Laid-out size in framebuffer pixels; `0` until the window first renders |
| `setmark(list, index, marked)` / `getmark(list, index)` | List, row index, boolean | Zero / boolean; per-row highlight independent of the cursor |
| `getfocus(window)` | Window handle | Handle of the window's focused text box or list, or `0` |
| `alive(widget)` | Handle | `true` while the widget exists (and, for a window, is not closing) |
| `mouseheld(widget)` | Handle | 1 while the left mouse button is held down in the widget's window, else 0; a `"motion"` callback uses it for drags |
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
| `"double"` | Canvas | canvas, x, y of the second press of a double click (within 0.45 s and 5 pixels; its `"pointer"` event comes first) |
| `"double"` | List | list, the row's index, when the same row is pressed twice within 0.45 s (its `"change"` event comes first) |
| `"menu"` | Canvas | canvas, x, y of a right-button press |
| `"motion"` | Canvas | canvas, x, y of the cursor as it moves over the canvas (at most once per frame, the latest position; `-1, -1` when it leaves) |
| `"key"` | Window | window, GLFW key code (key press) |
| `"keyup"` | Window | window, GLFW key code (key release) |
| `"focus"` | Window | window, `1` when focused, `0` when focus is lost |
| `"char"` | Window | window, Unicode code point of a typed character (any script; not sent for control keys or while Ctrl shortcuts are held) |

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

Toolbars dock at the top of their window, one below another. Drag the dotted
grip at the left of a toolbar to reorder it. Short button labels use compact
square buttons, `|` creates a vertical separator, and `buttonhint` displays
a tooltip while the pointer is held over a button. `setmovable(false)` keeps
a toolbar in its current position. A toolbar's buttons are
placed left to right and behave like any other button (`"click"` callbacks,
`settext`, `setchecked`, `inject`).

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

## Outline fonts

`loadfont` reads a TrueType font from its file, for example
`C:\Windows\Fonts\arial.ttf` or a file under `/usr/share/fonts`, and
`canvasfonttext` draws UTF-8 text with it. Glyphs are rasterized with
anti-aliasing in the runtime (no hinting, no kerning) and cached as textures
per window; fonts with CFF outlines (most `.otf`) are refused. Typed text
comes from the window's `"char"` event, which delivers code points beyond
ASCII; `"key"` still reports every key press.

## Image format

`image` currently loads binary P6 PPM data with a maximum component value of
255. Other image formats are rejected instead of silently ignored.
