# Changelog

## 0.4.0-alpha
- Added a toolbar (`tulatoolbar`) with Line, Circle, Load DXF, Save DXF,
  Zoom +, Zoom -, Fit and pan buttons, the active tool shown pressed, and
  keyboard shortcuts (`tulakey`). The example uses it.
- Added `tests/gui/tula_toolbar_test.su`.
- `ussr src/tula/tula.su [drawing.dxf]` starts Tula directly.

## 0.3.0-alpha
- Renders DXF drawings: `tulaload` reads LINE, CIRCLE, ARC, LWPOLYLINE,
  POLYLINE, POINT, TEXT/MTEXT and ELLIPSE from ASCII DXF and names the
  entities it skips. Saving writes every kind back.
- Zoomable view: mouse-wheel zoom about the cursor, `tulazoom`, `tulapan`
  and `tulafit`. The grid step adapts (1-2.5-5 x 5 mm) to keep lines 10 to
  100 pixels apart, and snapping follows the visible grid.
- Added `tests/gui/tula_dxf_test.su` and DXF fixtures.

## 0.2.0-alpha
- Rewrote Tula in USSR. `tula.c` and its temporary PPM canvas are gone;
  `tula.su` draws on Petersburg's vector canvas and writes DXF with the
  standard I/O library. The `tulainitialize`, `tulatool`, `tulaclick` and
  `tulasave` commands keep their arguments; `tulainitialize` now returns a
  canvas handle instead of an image handle.
- The drawing origin follows the bottom-left of the laid-out canvas, so the
  grid and drawing stay aligned when the window is maximized.
- Added an automated test, `tests/gui/tula_test.su`.

## 0.1.0-alpha
- Added a working Petersburg-based 2D CAD app with a snap grid, interactive
  line/circle tools, DXF 2000 export, and maximized startup.

## 0.1.0-alpha
- Opened project scaffold, package manifest, proposed API, example, and test plan.
