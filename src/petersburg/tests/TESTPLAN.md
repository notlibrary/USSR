# Petersburg tests

- Parse `petersburg.su` and `examples/hello.su` with the USSR parser.
- Build with GLFW/OpenGL and without GLFW (disabled-backend diagnostic).
- Validate all widget constructors, getters, mutators, and stale-handle errors.
- Check vertical packing at initial and resized window dimensions.
- Verify click/change callbacks execute through the scheduler and can mutate
  other widgets.
- Verify window close, explicit destroy, message-box close, and scheduler
  cleanup release all native windows and widget resources.
- Verify binary P6 PPM loading and invalid-image diagnostics.

The GLFW-dependent interaction checks require a display and must be run on both
Windows and Linux CI hosts. Text-entry coverage currently uses ASCII.
