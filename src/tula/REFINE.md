# Tula — refinement ideas

**Today:** a small 2D CAD on a Petersburg canvas (0.4.0-alpha). It has
Line and Circle tools with a 5-unit snap grid, and reads and writes ASCII
DXF (LINE, CIRCLE, ARC, LWPOLYLINE/POLYLINE, POINT, TEXT/MTEXT,
ELLIPSE). Zoom about the cursor and the adaptive grid work. There is
fit, pan, a toolbar, keyboard shortcuts, and up to 10 000 entities.

**Refine pass 1 (0.5.0-alpha):** the engine in `more.su`, used by the window's
command line and by scripts; [x] done, [~] partly done. See CHANGELOG.md and
`tests/tula_more_test.su`.

## Drawing tools
- [~] Arc (3-point, centre-start-end), polyline, rectangle, polygon, ellipse
  and spline tools — partly: arc (3 points), pline, rect, polygon, ellipse and spline as commands (the command line and scripts), not mouse tools yet
- [~] Text tool with height and rotation — partly: text with a height and a rotation (kept in DXF, not drawn turned) as commands (the command line and scripts), not mouse tools yet
- [~] Dimensions: linear, aligned, radius, diameter and angle (DXF DIMENSION) — partly: linear, aligned, radius, diameter and angle dimensions, written as lines and text, not DXF DIMENSION entities
- [~] Hatches and solid fills — partly: hatch: solid and ANSI31 HATCH entities; drawn as lines
- [x] Construction lines (XLINE/RAY)
- [~] A point tool and a multi-line (MLINE) tool — partly: point; no MLINE
- [x] Typed coordinates: `@10,0`, `@25<45`, absolute `100,50`, in a command
  line like AutoCAD's

## Snapping and precision
- [x] Object snaps: endpoint, midpoint, centre, intersection,
  perpendicular, tangent, nearest
- [~] Ortho mode and polar tracking (angle steps) — partly: ortho on in clicks; polar tracking is tl_polar_track, not in clicks yet
- [~] Snap toggle and grid step setting, plus a metric/imperial unit setting — partly: units mm|inch ($INSUNITS); no grid step setting
- [~] A live coordinate readout and the length/angle of the segment being
  drawn — partly: tl_readout and the dist command; not live under the pointer

## Editing
- [x] Selection by click and window/crossing box, plus a selection set
- [x] Move, copy, rotate, scale and mirror
- [x] Offset, trim, extend, fillet, chamfer, break and join
- [x] Delete and undo/redo
- [~] Grip editing: drag endpoints and centres — partly: tl_grips and tl_grip_move; no dragging in the window
- [~] Properties panel: colour, layer, linetype and lineweight per entity — partly: color, ltype, lweight, onlayer commands; no panel
- [x] Arrays: rectangular and polar

## Organisation
- [x] Layers: create, rename, colour, on/off, freeze, lock (DXF LAYER table)
- [x] Blocks: define, insert (INSERT entities read from DXF `BLOCKS`, which
  today are ignored), explode
- [~] Linetypes (dashed, centre) and lineweights in rendering — partly: linetypes drawn and written (LTYPE table); lineweights kept, not drawn
- [~] Named views — partly: view save and restore in the drawing; the window does not follow them

## Files
- [x] DXF R12 output for old tools, alongside DXF 2000
- [~] Read binary DXF — partly: tl_dxf_binary turns binary DXF into text; Load does not call it yet
- [~] SVG and PDF export, and PNG export through an offscreen canvas — partly: SVG and PDF (tl_svg, tl_pdf); no PNG
- [x] Import SVG paths
- Print with scale and paper size
- [~] Autosave and a recent files list — partly: tl_autosave and a recent files list (loads are recorded); no timer

## Measurement
- [x] Distance, area and perimeter queries
- [x] Entity information (list) for the selection
- [x] Bounding box and centroid

## Interface
- A layers panel and a properties panel (Petersburg widgets)
- [~] Context menus and a command palette — partly: a command line below the canvas (help lists the commands); no context menus
- Pan with the middle mouse button drag
- [x] Dark and light canvas themes
- [~] Higher entity limits with spatial indexing (a quadtree) for fast hit
  tests and redraws — partly: tl_qt_build and tl_qt_query; picking still walks every entity

## Scripting
- [x] A USSR scripting API to build drawings from code (`tula_line(_): x1
  y1 x2 y2`), headless, to write DXF from scripts
- Parametric shapes from a script with sliders

## Tests
- Round-trip DXF tests against files from LibreCAD/QCAD
- [x] Geometry unit tests (intersections, trim) without a window

## Constraints and parametrics
- [~] Geometric constraints: horizontal, vertical, parallel,
  perpendicular, coincident, tangent, equal length — partly: horizontal, vertical, length, parallel, perpendicular, coincident, equal on lines; no tangent
- [~] Driving dimensions: change a dimension's value and the geometry
  follows — partly: tl_drive sets a line's length and solves again
- [x] Named variables and expressions in sizes (`width = 2 * a + 10`)
- [x] Sketch regions detected as closed profiles, with area and centroid

## Beyond flat drawings
- [~] Isometric drafting mode (isoplanes, iso circles) — partly: tl_iso points and iso circles; no isoplane mode in the window
- [~] 3D extrude and revolve of a profile with an OpenGL preview, and STL
  export for printing — partly: tl_stl_extrude and tl_stl_revolve write ASCII STL; no 3D preview
- [~] G-code export for laser cutters and CNC routers (lines and arcs, tool
  offset), and SVG for plotters — partly: tl_gcode: lines, arcs, circles, polylines; no tool offset
- [~] Gerber export or a small schematic symbol library for electronics — partly: resistor, capacitor and ground symbols; no Gerber
- [x] Sheet templates: frames and title blocks (ГОСТ/ISO formats A4–A0)
- Paper space: several scaled views of the model on one sheet

## Drawing aids
- [x] Colours by layer or by entity, with an AutoCAD colour index table
- Text styles and TrueType fonts through Petersburg (Pobeda for
  drawings)
- Raster image underlay (trace a scan), via Omsk decoding
- [x] Symbols library: bolts, bearings, electrical and architectural
  symbols as blocks
- [~] Tolerances, surface finish and welding symbols — partly: surface roughness and fillet weld symbols; no tolerance frames
- [x] Tables and a bill of materials from block attributes
- [x] A "purge" of unused layers and blocks, and an "audit" of bad
  entities
- [x] Keep unsupported DXF entities on save (today they are named in the
  status line and dropped)
