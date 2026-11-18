# Changelog

## 0.8.1-alpha
- `mouseheld(widget)`: 1 while the left mouse button is held down in the
  widget's window, for drags in a "motion" callback.

## 0.8.0-alpha
- `filedialog.su`: the standard Open and Save dialog,
  `filedialog(path): mode title start filters`.
  - It shows the files and folders (`..` first, folders before files) and
    the Open / Save and Cancel buttons, with a box for the chosen path and
    a filter, which takes "label|patterns" pairs.
  - It is modal: it returns the chosen path, or "" on Cancel, Escape or
    closing it.
  - Save asks "Replace?" before an existing file, and adds the filter's
    extension to a bare name.
  - It opens on the list, on its first row, so the arrows move at once.
    A double click on a row opens it (a folder too), and typing over the
    list starts a name in the box.
- Lists: a double click sends a "double" event with the row's index, and
  a held Up or Down arrow keeps moving the selection. Typed characters no
  longer go into a focused list's items. `inject` takes "keyrepeat".
- `setmodal(window, true|false)`: while a modal window is open, the other
  windows take no input, and a click on them brings it to the front.
- `buttonicon(button, picture)`: a picture before a button's text, on
  toolbars too. "folder" and "disk" (a diskette) are built in, and a .ppm
  file works with magenta as the transparent colour.
- `grid(widget, row, 2, expand)`: column 2 spans the whole row.
- `toolmenu(toolbar, label, items)`: a drop-down menu on a toolbar.
  - `items` are separated by "|", and "-" draws a line.
  - Picking an item (a click, or Up/Down and Enter) calls the menu's
    "select" callback with the item's index. Escape or a click elsewhere
    closes it.
  - `setitems(menu, items)` replaces the items.
- `recent.su`: the files a program used last, kept in `~/.<app>-recent`.
  - `recentfiles(app)` and `recentadd(app, path)` read and update the list.
  - `filemenuitems(list)` builds a standard File menu: New, Open...,
    Save, Save As..., a line, the last 5 files, a line, Exit.
  - `filemenuaction(index, list)` turns a picked item into an action.

## 0.7.1-alpha
- Windows are drawn only when something may have changed, at most 120
  times a second. Before, every scheduler pump drew every window and
  waited for the display's vertical sync, so a program with a window open
  ran in 16 ms steps (200 x `sleep(5)` took 10 s; now 1 s). Something may
  have changed after a Petersburg call that is not a read, input, a
  resize or an expose.
- Windows: a millisecond timer (`timeBeginPeriod(1)`), and the scheduler's
  clock from the performance counter (`GetTickCount64` moves in 15.6 ms
  steps), so `sleep` and timers are on time.

## 0.7.0-alpha
- A held key sends its repeats (`GLFW_REPEAT`) to the window's `"key"`
  callback, so holding an arrow keeps moving in every program; text
  widgets still act on the first press only.
- A canvas `"motion"` event: canvas, x, y as the cursor moves over it, at
  most once a frame (the latest position), and -1, -1 when it leaves.
- `canvascursor(canvas, "hidden" | "normal")` hides the system cursor over
  a canvas, for programs that draw their own.
- `canvaskeep(canvas)` and `canvasback(canvas)`: mark the shapes drawn so
  far, then drop only what was added after the mark, so that a moving
  overlay is redrawn alone.
- `canvasdash` and `canvasdashcircle`: dashed lines and circles, cut into
  segments natively.

## 0.6.0-alpha
Refine pass 1 from REFINE.md: `more.su`, in USSR on top of the native
widgets. The scene, the rasteriser, the layout and the widget models need
no window.
- A retained scene (`ps_*`): shapes with ids that change or go one at a
  time, layers and z-order, transforms, clipping, hit testing, a dirty
  box; polylines, polygons (even-odd and non-zero fills), Béziers,
  circles, arcs, text and images; line widths and dashes; gradients and
  alpha. `ps_draw` puts it on a canvas.
- A software rasteriser (`ps_raster`) for headless CI and golden images,
  with PNG (through Omsk) and PPM output and a CRC; a terminal backend
  (`ps_tui`) with box glyphs; a scene as text and back.
- Layout (`pl_*`): boxes with spacing, padding, alignment, weights, min
  and max; grids with spans and weights; anchors; right-to-left
  mirroring; a DPI scale.
- Themes (`pt_*`): light, dark and high contrast, with an accent.
- Canvas widgets (`pw_*`), drawn into a scene and driven by pointer, wheel
  and keys: progress, slider, spin, radio, tabs, group, splitter, status
  bar, tooltip, text area (selection, cut/copy/paste), tree, table
  (sorting, resizing), scroll view, date and colour pickers, menu, rich
  label, file chooser.
- Services (`pb_*`): an undo stack (merging typing, a limit, a saved
  mark), a command table with accelerators and menus, data binding,
  easing and tweens, timers (`pb_every`), a clipboard shared by
  callbacks, jobs with progress and cancel, Tab focus order, event
  recording and replay, the widget tree (`pb_make`, `pb_parent`,
  `pb_children`), a declarative UI text (`kind{key=value, ...}`) built
  into native widgets, and images from memory.
- `examples/gallery.su` shows every control; `--shot FILE` renders the
  canvas widgets to a PNG with no window.
- `tests/petersburg_more_test.su` (132 checks, headless) and
  `tests/gui/petersburg_scene_test.su`.

## 0.5.0-alpha
- Outline fonts from the system: `fontinfo`, `loadfont`, `fontwidth`,
  `fontheight` and `canvasfonttext` draw anti-aliased TrueType text (`.ttf`,
  `.ttc`) through a small built-in reader (`src/ttf.h`).
- Added the window `"char"` event: typed characters as Unicode code points.

## 0.4.0-alpha
- Added `toolbar(window)`: a strip docked at the top of the window whose
  buttons sit side by side. Buttons accept `setchecked`/`getchecked` and are
  drawn pressed while checked.
- Added `getfocus(window)`.

## 0.3.0-alpha
- Added the canvas widget: retained lines, circles, arcs, rectangles, filled
  rectangles and text, with canvas pointer and mouse-wheel events.
- Added window `"keyup"` and `"focus"` events, list row marks
  (`setmark`/`getmark`), laid-out size queries, `alive`, and `inject` for
  driving the input path from tests.
- Added `uilock`/`uiunlock`/`uiidle` to serialize callback processes.
- Timers (`sleep`) now wake while a window is open; previously the scheduler
  only pumped GUI input until every window closed.
- Tver and Tula no longer need native code of their own: Petersburg is the only
  GUI component compiled into `ussr`.

## 0.2.0-alpha
- Added the GLFW/OpenGL window and widget runtime with responsive vertical packing.
- Added script-facing widget, text, image, message-box, and lifecycle commands.
- Routed UI callbacks through the USSR scheduler.
- Added maximized windows, dynamic image reloads, and image pointer callbacks.
- Added optional GLFW/OpenGL detection to the Autotools and GNU make builds.

## 0.1.0-alpha
- Opened the project scaffold, package manifest, proposed API, example, and test plan.
