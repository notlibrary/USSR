# tver test plan

- Package manifest parses as UNO (not JSON).
- Entry script parses with the target USSR grammar once API syntax is finalized.
- Each public command returns a value on success and failure.
- Invalid handles/arguments return clear errors.
- Resource cleanup is safe when windows/documents/panels are closed.
- Platform adapter tests run on Windows and Linux.
- Scheduler integration does not block the main event loop.

## Project-specific

- Navigate directories; copy/move/rename; cancellation; overwrite confirmation; permission errors; symlink handling.
- Large directories remain responsive; never delete without explicit request/confirmation policy.
