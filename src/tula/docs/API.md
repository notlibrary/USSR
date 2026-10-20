# Tula API

Include `tula.su`. The runnable example in `examples/hello.su` is the
recommended starting point.

| Command | Arguments | Behavior |
|---|---|---|
| `tulainitialize(canvas)` | Window, status label, filename text box | Creates the drawing canvas and returns its Petersburg canvas handle |
| `tulatool(_)` | `"line"` or `"circle"` | Selects the drawing tool and starts a fresh point sequence |
| `tulaclick(_)` | Canvas handle, canvas x, canvas y | Snaps and consumes a canvas pointer event |
| `tulasave(_)` | None | Writes the drawing to the filename field as ASCII DXF 2000 |
| `tulaload(_)` | None | Reads the DXF named in the filename field, replaces the drawing and fits it in view |
| `tulawheel(_)` | Canvas handle, x, y, direction | Canvas `"wheel"` callback: zooms 1.25x in (`1`) or out (`-1`) about x, y |
| `tulazoom(_)` | Factor | Zooms about the canvas center |
| `tulapan(_)` | dx, dy in canvas pixels | Moves the view |
| `tulafit(_)` | None | Fits the whole drawing in the canvas with a 5% margin |
| `tulatoolbar(toolbar)` | Window | Creates the toolbar described below; call after `tulainitialize` |
| `tulakey(_)` | Window, GLFW key code | Window `"key"` callback: arrow keys pan, `=`/`-` (and keypad `+`/`-`) zoom, `F` fits |

Line input is two canvas clicks; circle input is center then radius point.
Points snap to the visible minor grid. Drawing units are millimeters; at 1x
zoom one millimeter is 5 pixels and the origin starts at the lower-left of
the canvas once the window has been laid out. Zoom is limited to 0.001 to
1000 pixels per millimeter.

## Toolbar

`tulatoolbar` creates a Petersburg toolbar, docked above the filename box and
canvas, with these buttons:

| Button | Action |
|---|---|
| Line, Circle | `tulatool`; the active tool's button is drawn pressed |
| Load DXF, Save DXF | `tulaload` / `tulasave` with the filename box |
| Zoom +, Zoom - | `tulazoom` 1.25 / 0.8 about the canvas center |
| Fit | `tulafit` |
| Left, Right, Up, Down | `tulapan` by 100 pixels, showing more of the drawing on that side |

Handles and labels are kept in the `tula_buttons` and `tula_button_names`
hash vectors in creation order.

## Grid

The minor grid step follows a 1-2.5-5 sequence of multiples of 5 mm (..., 1,
2.5, 5, 10, 25, 50, 100, ...) chosen so minor lines stay 10 to 100 pixels
apart; every fourth line is a darker major line. The grid and the drawing are
drawn with the same world-to-canvas transform, so they stay aligned at every
zoom and pan. The status line shows the zoom and the grid step after each
view change.

## DXF

Reading accepts ASCII DXF with LF or CRLF line endings. Entities are taken
from the `ENTITIES` section only.

| DXF entity | Rendering |
|---|---|
| `LINE` | Line |
| `CIRCLE` | Circle |
| `ARC` | Arc, counter-clockwise from group 50 to 51 degrees |
| `LWPOLYLINE` | Polyline; closed when flag 70 has bit 1. Bulges (group 42) are drawn as straight segments |
| `POLYLINE` + `VERTEX` + `SEQEND` | Polyline from the vertices' 10/20 groups (2D) |
| `POINT` | Small cross |
| `TEXT`, `MTEXT` | Group 1 text at the insertion point in Petersburg's single bitmap font size; MTEXT formatting codes are shown as written |
| `ELLIPSE` | Polyline with 64 segments per full turn (groups 10/20, 11/21, 40, 41, 42) |

Everything else (`SPLINE`, `INSERT` block references, `HATCH`, `DIMENSION`,
`SOLID`, 3D entities, ...) is skipped and named in the status line, e.g.
`2 unsupported entities skipped (SPLINE, INSERT)`. Layers, colors, line types,
Z coordinates and extrusion directions are ignored. Binary DXF, truncated
files and malformed numbers are refused with a status message and leave the
current drawing unchanged.

Writing produces `LINE`, `CIRCLE`, `ARC`, `LWPOLYLINE`, `POINT` and `TEXT` on
layer `0`, with `$ACADVER` `AC1015` (DXF 2000) and `$INSUNITS` 4
(millimeters). Files use LF line endings on every platform; numbers carry up
to ten decimals (fewer for large values where the host integer is 32 bits).
