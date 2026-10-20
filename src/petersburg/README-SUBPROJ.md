# USSR desktop subprojects

- **Petersburg** (`petersburg.su`) has a GLFW/OpenGL native widget runtime
  and scheduler integration. It is the only GUI component compiled into
  `ussr`. See its README and API contract for build details.
- **Tver** (`tver.su`), a two-panel file manager, and **Tula** (`tula.su`), a
  2D CAD, are written entirely in USSR on top of Petersburg and the standard
  library.

Petersburg does not yet provide native accessibility or Unicode text input.
