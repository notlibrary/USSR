# Changelog

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
