# Tula test plan

- Build with and without the GLFW backend.
- Parse and execute `examples/hello.su`; verify the window maximizes and the
  grid canvas, tool buttons, filename field, and status are visible.
- Click two points with Line selected and verify the displayed snapped line.
- Click a center and radius point with Circle selected and verify the displayed
  snapped circle.
- Save a mixed drawing and verify the DXF contains `AC1015`, one `LINE` and one
  `CIRCLE`, correct coordinates/radius, `$INSUNITS` 4, and `EOF`.
- Verify empty filename, unwritable destination, zero-radius circles, repeated
  points, and the entity limit report errors without corrupting the drawing.
- Verify the temporary canvas is removed at interpreter cleanup.
