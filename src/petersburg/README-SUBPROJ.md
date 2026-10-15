# USSR desktop subprojects — alpha scaffolds

This archive opens three related projects:

- **Petersburg** (`petersburg.su`): cross-platform GUI widgets and window/event API.
- **Tver** (`tver.su`): two-panel keyboard-friendly file manager built on Petersburg.
- **Tula** (`tula.su`): general-purpose 2D CAD built on Petersburg.

Each folder contains a UNO `box.uno` package manifest, `.su` API scaffold, README, API contract, example, changelog, and test plan.

**Important:** these are project scaffolds, not runnable releases. The proposed commands have not yet been registered in the USSR C interpreter, the `.su` examples may need syntax adjustments to the current parser, and no GUI/filesystem/CAD native implementation is included. Petersburg's backend choice (Win32 + GTK is proposed) needs to be confirmed and built before Tver or Tula can run.

## Recommended implementation order

1. Petersburg native C ABI and event-loop integration with USSR scheduler.
2. Windows Win32 and Linux GTK adapters, plus a common widget handle model.
3. Tver filesystem primitives and async copy/move operations.
4. Tula geometry core and headless unit tests, then drawing canvas and SVG export.
