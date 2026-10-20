# Tula test plan

Automated (GLFW build and a display required):

- `ussr tests/gui/tula_test.su`: tool-required message, snapped line and
  circle input, degenerate line refusal, unknown tool refusal, the exact DXF
  output, and the empty filename error.
- `ussr tests/gui/tula_dxf_test.su`: loads `tests/gui/fixtures/sample.dxf`
  (every supported entity, a `BLOCKS` section that must be ignored, and two
  unsupported entities), checks the parsed entities and the number of shapes
  drawn, fit, zoom about the cursor, grid spacing across 75 zoom steps, zoom
  limits, snapping to the visible grid, pan, save/load round trip, CRLF
  input, and refusal of missing, truncated, binary and malformed files.

Manual:

- Run `examples/hello.su`; verify the window maximizes and the grid canvas,
  toolbar, filename field, and status are visible.
- Load `tests/gui/fixtures/sample.dxf`, zoom with the mouse wheel over
  different points and confirm the point under the cursor stays put and the
  grid stays aligned with the geometry.
- Verify an unwritable destination reports an error without corrupting the
  drawing, and that the entity limit is enforced.
