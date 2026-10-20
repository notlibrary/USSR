# Tula API

Include `tula.su`. The runnable example in `examples/hello.su` is the
recommended starting point.

| Command | Arguments | Behavior |
|---|---|---|
| `tulainitialize(canvas)` | Window, status label, filename text box | Creates the drawing canvas and returns its Petersburg canvas handle |
| `tulatool(_)` | `"line"` or `"circle"` | Selects the drawing tool and starts a fresh point sequence |
| `tulaclick(_)` | Canvas handle, canvas x, canvas y | Snaps and consumes a canvas pointer event |
| `tulasave(_)` | None | Writes current line/circle entities to the filename field as ASCII DXF 2000 |

Line input is two canvas clicks; circle input is center then radius point.
Both points snap to a 5-unit grid. The drawing coordinate system uses
millimeters at 5 pixels per unit, with its origin at the lower-left of the
canvas once the window has been laid out. DXF `LINE` and `CIRCLE` entities are
placed on layer `0`. `$ACADVER` is set to `AC1015` (DXF 2000), and
`$INSUNITS` is set to 4 (millimeters). Files are written with LF line endings
on every platform; numbers carry up to ten decimals.

Petersburg's canvas `"pointer"` callback receives the canvas handle plus x/y
in canvas pixels. The example also uses Petersburg's `maximize(window)`
function before `run()`.
