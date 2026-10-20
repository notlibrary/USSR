# Tula test plan

Automated: `ussr tests/gui/tula_test.su` (GLFW build and a display required)
covers the tool-required message, snapped line and circle input, degenerate
line refusal, unknown tool refusal, the exact DXF output, and the empty
filename error.

Manual:

- Build with and without the GLFW backend.
- Run `examples/hello.su`; verify the window maximizes and the grid canvas,
  tool buttons, filename field, and status are visible.
- Click points and verify the drawn geometry matches the grid snap.
- Verify an unwritable destination reports an error without corrupting the
  drawing, and that the entity limit is enforced.
