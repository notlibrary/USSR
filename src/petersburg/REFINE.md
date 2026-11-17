# Petersburg — refinement ideas

**Today:** a GLFW/OpenGL widget library (0.5.0-alpha).
- Widgets: windows, label, button, textbox, checkbox, listbox (with
  marks), combobox, a P6 PPM image, a toolbar, and a retained canvas
  (lines, circles, arcs, rects, text).
- Fonts are a bitmap font plus TrueType outline fonts (`loadfont`,
  `canvasfonttext`).
- Events: click, change, key, keyup, char, focus, pointer, wheel. Layout
  is `pack` and a two-column `grid`.
- Also: a message box, maximise/fullscreen, `inject` for tests, and
  `uilock` to serialise callbacks.

**Refine pass 1 (0.6.0-alpha):** `more.su` (USSR on top of the native widgets);
[x] done, [~] partly done. See CHANGELOG.md and `tests/petersburg_more_test.su`.

## Widgets
- [~] Multi-line text area with scrolling, selection and clipboard — partly: pw_textarea: lines, cursor, selection, scrolling and cut/copy/paste; drawn on a canvas by more.su (pw_*), not a native widget
- [~] Spin box, slider, progress bar, radio buttons with groups — partly: pw_spin, pw_slider, pw_progress, pw_radio; drawn on a canvas by more.su (pw_*), not a native widget
- [~] Tabs (notebook), group box/frame, splitter panes — partly: pw_tabs, pw_group, pw_splitter; drawn on a canvas by more.su (pw_*), not a native widget
- [~] Tree view (for Tver, KGB, Kremlin) — partly: pw_tree with open/close by click and keys; drawn on a canvas by more.su (pw_*), not a native widget
- [~] Table/grid widget with columns, sorting and resizing (Siberia,
  `kgb table`) — partly: pw_table: columns sorted by click, resized by drag; drawn on a canvas by more.su (pw_*), not a native widget
- [~] Menu bar and context menus as real widgets (Kremlin draws its own) — partly: pw_menu and pb_command_menu; drawn on a canvas by more.su (pw_*), not a native widget
- [~] Scroll view container for any widget — partly: pw_scroll: content offsets and the wheel; drawn on a canvas by more.su (pw_*), not a native widget
- [~] Status bar widget — partly: pw_status; drawn on a canvas by more.su (pw_*), not a native widget
- [~] Tooltip on hover for every widget — partly: pw_tooltip with a hover delay; drawn on a canvas by more.su (pw_*), not a native widget
- [~] Date picker and colour picker — partly: pw_date and pw_colour (HSV); drawn on a canvas by more.su (pw_*), not a native widget
- [~] Native-looking file open/save dialogs (Siberia has its own) — partly: pw_files: folders and a pattern; drawn on a canvas by more.su (pw_*), not a native widget

## Text and fonts
- Unicode text input in textboxes (the README says text input is ASCII)
- Text shaping for complex scripts and bidi (HarfBuzz-like or a small
  shaper)
- Font fallback chains (Cyrillic, CJK, emoji)
- [~] Rich text spans in labels (bold, colour) — partly: pw_rich: spans with a colour and bold; drawn on a canvas by more.su (pw_*), not a native widget
- IME support for Chinese, Japanese and Korean input
- Subpixel or gamma-correct anti-aliasing options, and a HiDPI scale
  factor

## Layout
- [~] Horizontal and vertical boxes with spacing, padding and alignment — partly: pl_box: spacing, padding, alignment; it gives rectangles, the native pack and grid are unchanged
- [~] A grid with any number of columns, row/column spans and weights — partly: pl_grid: spans and row and column weights, as rectangles
- [~] Minimum and maximum sizes, plus anchors — partly: pl_box min and max, pl_anchor; as rectangles
- [~] Constraint or flex layout — partly: pl_box weights grow like flex; no shrinking below the minimum
- [~] Automatic HiDPI scaling (Kremlin hand-codes 1x–3x) — partly: pl_scale gives a factor from the DPI; nothing scales by itself yet

## Canvas
- [x] Paths: polylines, polygons, Béziers, fills with even-odd/non-zero
  rules
- [~] Line widths, dash patterns, caps and joins — partly: width and dash patterns on the canvas and in the rasteriser; no caps or joins
- [x] Transforms (translate, rotate, scale) and clipping
- [~] Gradients and alpha blending — partly: gradients and alpha in the rasteriser; ps_draw approximates gradients by rows and has no alpha
- [~] Images on the canvas (PNG/JPEG through Omsk, not only PPM) — partly: image items (Omsk images) in the rasteriser; ps_draw leaves them out
- [x] Hit testing of shapes by id
- [x] Layers and z-order
- [x] An offscreen canvas exported to PNG (screenshots, Tula's export)

## Events and input
- Mouse drag events with button and modifier state, plus drag and drop
  (internal and from the OS)
- Double-click on all widgets (Kremlin has its own)
- Gamepad input via GLFW
- [~] Keyboard focus traversal with Tab and Shift+Tab across all widgets — partly: pb_tab moves the focus over a list of handles; the program binds Tab
- [x] Global shortcuts / accelerators table
- [~] Clipboard API (`clipboard_get`, `clipboard_set`) — partly: pb_clipboard_set and _get, shared by the program's callbacks; not the system's clipboard
- [x] Timers as first-class (`every(ms, "handler")`) next to `sleep`
- Window events: resize, move, minimise, close request with veto

## Architecture
- [~] Retained widget tree inspection (`children`, `parent`) for tests — partly: pb_make notes each widget it makes: pb_parent, pb_children, pb_kind
- [~] Themes: colours, fonts and metrics in one style table, plus dark mode — partly: pt_theme light, dark, contrast with an accent; the canvas widgets use it, the native ones do not
- Accessibility: expose widgets to screen readers (UIA / AT-SPI)
- [x] A software rasteriser backend for headless CI, with no GPU
- A Wayland backend check and a macOS (Cocoa/Metal or GL) build
- Moscow backend parity: the same API on Moscow's framebuffer
- [x] A declarative UI description (UNO literal) loaded into widgets
- Reduce callback-process cost: optional shared-state callbacks that do
  not copy variables

## Tests
- [x] Headless golden-image tests of every widget
- [~] Event replay recordings for GUI regression tests — partly: pb_record, pb_recording_text and pb_replay (through inject); nothing records the native input yet
- Fill in TESTPLAN's ASCII-only gap with Unicode input tests

## Windows and system
- Several windows with parent and child relations, modal dialogs that
  block their parent, and tool windows
- Window icon, title updates, minimum size, position and "always on
  top"
- System tray icon with a menu, and desktop notifications
- Custom mouse cursors per widget (text beam, hand, resize arrows)
- Standard dialogs: open, save, choose folder, choose font, choose
  colour, about, confirm with custom buttons (`messagebox` has one
  form)
- Remember and restore window geometry by a name
- Printing through the system's print dialog
- Screen information: monitors, DPI, work area

## Programming model
- [~] Data binding: a widget bound to a hash key updates when the value
  changes — partly: pb_model, pb_bind, pb_set, pb_pull: a btree model bound to canvas widgets or native text; not hash keys
- [x] An undo stack service for apps (Tula, Siberia and Novosibirsk each
  need one)
- [x] A command table: one named action with a label, shortcut, enabled
  state and toolbar button
- [~] Async work with a progress dialog and cancel, without freezing the
  window — partly: pb_job: steps, a percentage and cancel; the program runs it
- [x] Animation helpers: timed transitions with easing
- [x] An image widget that takes pixels from memory, not only a P6 file on
  disk
- [x] Canvas item ids: change or delete one retained shape (today
  `canvasclear` and redraw)
- [~] Double buffering control and partial redraws for large canvases — partly: ps_dirty gives the box that changed; ps_draw still redraws all
- [x] A widget gallery example showing every control, as living
  documentation

## Reach
- Touch and pen input (taps, pinch, pressure)
- OpenGL ES / WebGL build through Emscripten, so apps run in a browser
- [~] A terminal (TUI) backend drawing the same widgets with box glyphs — partly: ps_tui draws rectangles, lines and text with box glyphs; no other shapes
- [~] A remote display: draw on another machine over a socket — partly: ps_text and ps_from_text carry a scene as lines; no socket of its own
- [~] Right-to-left layout mirroring — partly: pl_mirror mirrors rectangles; nothing mirrors by itself
