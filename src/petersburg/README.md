# Petersburg (petersburg.su)

Petersburg is the proposed cross-platform GUI/widget layer for USSR. Its goal is to make ordinary desktop applications possible from `.su` scripts: forms, windows, buttons, text fields, labels, menus, dialogs, and event callbacks.

## Design goals

- One script-facing API across Windows and Linux; keep platform details behind a native adapter.
- Event-loop integration with the USSR scheduler; callbacks must not block the interpreter.
- Explicit widget handles and deterministic destruction.
- Layout containers rather than pixel positioning as the default.
- UTF-8 text, keyboard navigation, accessibility-friendly controls, and DPI-aware sizing.
- No shelling out to external processes to create or manage windows.

## Proposed API

`createwindow`, `label`, `button`, `textbox`, `checkbox`, `combobox`, `panel`, `menu`, `dialog`, `settext`, `gettext`, `show`, `close`, `on`, `run`, and `destroy`. Every command returns a USSR value; errors should be returned as values and not silently ignored. Exact call syntax must be reconciled with the current interpreter before implementation.

## Native backend

Use a small C ABI with opaque handles and platform-specific implementation files. Candidate backends: Win32 on Windows and GTK on Linux, selected at build time. Do not mix backend-specific handles into `.su` scripts. The initial alpha contains API proposals and tests/specifications, not a compiled backend.

## Example

See `examples/hello.su` for intended API shape.
