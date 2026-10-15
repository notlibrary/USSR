# tula API contract draft

Package: `tula`

## Proposed public commands

- `tula_createwindow`
- `tula_newdrawing`
- `tula_open`
- `tula_save`
- `tula_line`
- `tula_polyline`
- `tula_circle`
- `tula_arc`
- `tula_rectangle`
- `tula_text`
- `tula_dimension`
- `tula_layer`
- `tula_snap`
- `tula_undo`
- `tula_redo`
- `tula_zoom_fit`
- `tula_export_svg`

## Return values and errors

Every command returns a USSR value. Commands that can fail should return a documented error value or status object; they must not silently discard errors. Handles should be validated before use. Destruction should be idempotent where practical.

## Status

This is a design contract, not an implementation guarantee. Check the package README before treating any command as available.
