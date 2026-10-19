# Tula API

Include `tula.su`. The runnable example in `examples/hello.su` is the
recommended starting point.

| Script wrapper | Native command | Arguments | Behavior |
|---|---|---|---|
| `tulainitialize(canvas)` | `tula_initialize` | Window, status label, filename text box | Creates the temporary drawing canvas and returns its Petersburg image handle |
| `tulatool(_)` | `tula_set_tool` | `"line"` or `"circle"` | Selects the drawing tool and starts a fresh point sequence |
| `tulaclick(_)` | `tula_canvas_click` | Canvas handle, canvas x, canvas y | Snaps and consumes a canvas pointer event |
| `tulasave(_)` | `tula_save_dxf` | None | Writes current line/circle entities to the filename field as ASCII DXF 2000 |

Line input is two canvas clicks; circle input is center then radius point.
Both points snap to a 5-unit grid. The drawing coordinate system uses
millimeters, has its origin at the lower-left of the visible canvas, and spans
200 by 140 units. DXF `LINE` and `CIRCLE` entities are placed on layer `0`.
`$ACADVER` is set to `AC1015` (DXF 2000), and `$INSUNITS` is set to 4
(millimeters).

Petersburg provides the `"pointer"` event on image widgets. Its callback
receives the image handle plus x/y coordinates in source-image pixels. The
example also uses Petersburg's `maximize(window)` function before `run()`.
