# Changelog

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
