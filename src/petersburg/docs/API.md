# petersburg API contract draft

Package: `petersburg`

## Proposed public commands

- `createwindow`
- `close`
- `show`
- `run`
- `label`
- `button`
- `textbox`
- `checkbox`
- `combobox`
- `panel`
- `menu`
- `dialog`
- `settext`
- `gettext`
- `on`
- `destroy`

## Return values and errors

Every command returns a USSR value. Commands that can fail should return a documented error value or status object; they must not silently discard errors. Handles should be validated before use. Destruction should be idempotent where practical.

## Status

This is a design contract, not an implementation guarantee. Check the package README before treating any command as available.
