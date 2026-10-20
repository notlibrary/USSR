# Changelog

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
