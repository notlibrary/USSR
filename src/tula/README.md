# Tula (tula.su)

Tula is a planned general-purpose 2D CAD application with a Petersburg-based GUI and a geometry core independent of the rendering backend.

## Initial scope

- Infinite/pannable drawing canvas with zoom and fit-to-drawing.
- Entities: point, line, polyline, circle, arc, rectangle, text, and dimensions.
- Layers, visibility/lock, selection, undo/redo, grid, object snaps, and orthogonal mode.
- Precise coordinates and units; avoid floating-point display drift.
- Save/load a documented native text format and export SVG; DXF import/export can follow.
- Deterministic geometry tests separated from GUI tests.

## Architecture

Keep model geometry, commands, file format, and GUI in separate modules. The geometry core must be testable headlessly. Use integer/fixed-point or double-precision internal coordinates as appropriate, with explicit unit conversion; the UI must not be the source of truth for geometry. Large files and expensive operations should cooperate with the USSR scheduler.

The initial alpha is a design scaffold with sample command syntax and test plan, not a functioning CAD engine.
