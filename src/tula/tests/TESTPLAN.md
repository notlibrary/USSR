# tula test plan

- Package manifest parses as UNO (not JSON).
- Entry script parses with the target USSR grammar once API syntax is finalized.
- Each public command returns a value on success and failure.
- Invalid handles/arguments return clear errors.
- Resource cleanup is safe when windows/documents/panels are closed.
- Platform adapter tests run on Windows and Linux.
- Scheduler integration does not block the main event loop.

## Project-specific

- Geometry calculations; snapping; undo/redo; save-load round trips; SVG export.
- Test degenerate entities, large coordinates, unit conversions, and malformed files.
