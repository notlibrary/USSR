# Changelog

## 0.7.0-alpha
- The toolbar starts with Open (a folder picture), Save (a diskette) and
  Save As, in place of Load DXF and Save DXF.
  - Open and Save As show Petersburg's file dialog, filtered to DXF
    drawings; it blocks Tula until it is answered.
  - Save writes the file named in the filename box (the dialog when the
    box is empty).
- A File menu starts the toolbar: New, Open..., Save, Save As..., the last
  5 drawings (from `~/.tula-recent`), and Exit. Saved drawings now go into
  the recent list too, not only loaded ones.

## 0.6.1-alpha
- The crosshair follows the mouse at the display's rate. The motion
  callback redraws at once (one redraw at a time; the newest position
  wins) instead of waiting for the 15 ms loop. Together with Petersburg
  0.7.1, a move went from about 150 ms to about 25 ms through the real
  event path, one or two 60 Hz frames.

## 0.6.0-alpha
- `scope.su`: a crosshair in place of the mouse arrow, as LibreCAD draws
  one: two dashed lines across the canvas, in another colour, through the
  point a click would take (grid and object snaps applied), with a pick
  box at the centre and the coordinates beside it. The system cursor is
  hidden over the canvas.
- A live preview: after the first point, the line to the cursor (or the
  circle through it) is drawn dashed with its length or radius, and
  Escape drops the first point.
- Holding an arrow key keeps panning (key repeats reach the window now).
- The cursor uses Petersburg's new canvas `"motion"` event, redrawn at most
  once a frame by a small loop; `tests/gui/tula_scope_test.su`.
- Fast: a mouse move redraws only the crosshair and the preview. The
  drawing is kept on the canvas (`canvaskeep` / `canvasback`), and the
  dashes are cut natively (`canvasdash`, `canvasdashcircle`). On a drawing
  of 400 lines a move went from about 10 ms to well under 1 ms, with
  exactly the same shapes drawn.
- Object snaps no longer recompute everything on each move. The points
  that do not depend on the cursor (end, mid, center, perp and tan from
  the first point, and intersections of every pair of entities) are kept,
  indexed by x, until the drawing, the modes or the first point change.
  "near" is worked out only for entities whose box comes within reach. On
  120 entities with every mode on, a move went from about 150 ms to well
  under 50 ms, with the same snapped point (`tests/tula_osnap_test.su`
  compares the two searches).
- A `ussr` built before these Petersburg calls says so in Tula's status
  line, instead of quietly redrawing everything on each move.

## 0.5.0-alpha
Refine pass 1 from REFINE.md: the CAD engine in `more.su` (no window
needed), used by a new command line below the canvas and by scripts.
- Commands (Enter in the command line; `help` lists them): line, pline,
  circle, arc (3 points), rect, polygon, ellipse, spline, point, text
  (height, rotation), xline, ray, dimensions (linear, aligned, radius,
  diameter, angle), hatch; select (pick, all, last, window, crossing),
  move, copy, rotate, scale, mirror, rectangular and polar arrays,
  offset, trim, extend, fillet, chamfer, break, join, erase, explode,
  undo, redo; layers (new, set, on/off, freeze, lock, colour, linetype,
  rename), colour, linetype and lineweight per entity; blocks, insert,
  a symbol library (bolt, bearing, resistor, capacitor, ground, door,
  window, roughness, weld); A4-A0 sheets with a title block; distance,
  area, perimeter, list, bounding box, centroid; named views, units,
  purge, audit, a bill of materials table, themes, object snaps and
  ortho for clicks. Coordinates take `@10,0`, `@25<45` and expressions;
  `NAME = EXPRESSION` sets a variable.
- Drawing follows layers (on, frozen), colours (the ACI table), and
  linetypes; the canvas has a dark theme.
- DXF: INSERT entities and BLOCKS are read and drawn, layers and their
  linetypes are kept, other entities are kept and written back; DXF
  2000 (with layers, linetypes, blocks, splines, ellipses, hatches) and
  R12 output; binary DXF converts to text.
- Also: SVG and PDF output, SVG import, G-code, STL extrude and revolve,
  geometric constraints with driving dimensions, closed regions from
  lines, isometric helpers, a quadtree, autosave and recent files, and a
  scripting API (`tula_new`, `tula_line`, `tula_circle`, `tula_rect`,
  `tula_do`, `tula_save`, ...).
- `tests/tula_more_test.su` (124 checks). The DXF fixtures' INSERT now
  draws inside the drawing, and the GUI tests count it.

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
