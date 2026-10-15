# Tver (tver.su)

Tver is a two-panel file manager built on Petersburg. It aims to combine the discoverability of a GUI file manager with keyboard-driven workflows familiar from Total Commander and Midnight Commander.

## Planned features

- Left/right panels with independent paths, selection, sorting, and filters.
- Keyboard navigation, function-key actions, bookmarks, and location entry.
- Copy, move, rename, mkdir, delete, properties, and file viewing.
- Conflict prompts, overwrite policies, progress/cancel, and error reporting.
- Drives/volumes on Windows; mounted filesystems on Linux.
- Safe path handling, no shell-based file operations, and no silent destructive actions.
- Optional archive and remote-provider adapters in later releases.

## Architecture

The `.su` layer owns panel state and commands. Petersburg renders the interface and dispatches events. A native filesystem layer performs operations with explicit result values. File operations should be cancellable and must not block Petersburg's UI loop; use the USSR scheduler for asynchronous work.

The initial alpha is a design scaffold, not a finished file manager.
