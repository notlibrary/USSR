# Tula — refinement ideas

**Today:** a small 2D CAD on a Petersburg canvas (0.4.0-alpha). It has
Line and Circle tools with a 5-unit snap grid, and reads and writes ASCII
DXF (LINE, CIRCLE, ARC, LWPOLYLINE/POLYLINE, POINT, TEXT/MTEXT,
ELLIPSE). Zoom about the cursor and the adaptive grid work. There is
fit, pan, a toolbar, keyboard shortcuts, and up to 10 000 entities.

## Drawing tools
- Arc (3-point, centre-start-end), polyline, rectangle, polygon, ellipse
  and spline tools
- Text tool with height and rotation
- Dimensions: linear, aligned, radius, diameter and angle (DXF DIMENSION)
- Hatches and solid fills
- Construction lines (XLINE/RAY)
- A point tool and a multi-line (MLINE) tool
- Typed coordinates: `@10,0`, `@25<45`, absolute `100,50`, in a command
  line like AutoCAD's

## Snapping and precision
- Object snaps: endpoint, midpoint, centre, intersection,
  perpendicular, tangent, nearest
- Ortho mode and polar tracking (angle steps)
- Snap toggle and grid step setting, plus a metric/imperial unit setting
- A live coordinate readout and the length/angle of the segment being
  drawn

## Editing
- Selection by click and window/crossing box, plus a selection set
- Move, copy, rotate, scale and mirror
- Offset, trim, extend, fillet, chamfer, break and join
- Delete and undo/redo
- Grip editing: drag endpoints and centres
- Properties panel: colour, layer, linetype and lineweight per entity
- Arrays: rectangular and polar

## Organisation
- Layers: create, rename, colour, on/off, freeze, lock (DXF LAYER table)
- Blocks: define, insert (INSERT entities read from DXF `BLOCKS`, which
  today are ignored), explode
- Linetypes (dashed, centre) and lineweights in rendering
- Named views

## Files
- DXF R12 output for old tools, alongside DXF 2000
- Read binary DXF
- SVG and PDF export, and PNG export through an offscreen canvas
- Import SVG paths
- Print with scale and paper size
- Autosave and a recent files list

## Measurement
- Distance, area and perimeter queries
- Entity information (list) for the selection
- Bounding box and centroid

## Interface
- A layers panel and a properties panel (Petersburg widgets)
- Context menus and a command palette
- Pan with the middle mouse button drag
- Dark and light canvas themes
- Higher entity limits with spatial indexing (a quadtree) for fast hit
  tests and redraws

## Scripting
- A USSR scripting API to build drawings from code (`tula_line(_): x1
  y1 x2 y2`), headless, to write DXF from scripts
- Parametric shapes from a script with sliders

## Tests
- Round-trip DXF tests against files from LibreCAD/QCAD
- Geometry unit tests (intersections, trim) without a window
