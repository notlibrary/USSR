# tver API contract draft

Package: `tver`

## Proposed public commands

- `tver_createwindow`
- `tver_panel`
- `tver_refresh`
- `tver_select`
- `tver_copy`
- `tver_move`
- `tver_rename`
- `tver_mkdir`
- `tver_delete`
- `tver_view`
- `tver_bookmark`
- `tver_bind_keys`
- `tver_show`
- `tver_run`

## Return values and errors

Every command returns a USSR value. Commands that can fail should return a documented error value or status object; they must not silently discard errors. Handles should be validated before use. Destruction should be idempotent where practical.

## Status

This is a design contract, not an implementation guarantee. Check the package README before treating any command as available.
