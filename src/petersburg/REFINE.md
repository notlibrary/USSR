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

## Widgets
- Multi-line text area with scrolling, selection and clipboard
- Spin box, slider, progress bar, radio buttons with groups
- Tabs (notebook), group box/frame, splitter panes
- Tree view (for Tver, KGB, Kremlin)
- Table/grid widget with columns, sorting and resizing (Siberia,
  `kgb table`)
- Menu bar and context menus as real widgets (Kremlin draws its own)
- Scroll view container for any widget
- Status bar widget
- Tooltip on hover for every widget
- Date picker and colour picker
- Native-looking file open/save dialogs (Siberia has its own)

## Text and fonts
- Unicode text input in textboxes (the README says text input is ASCII)
- Text shaping for complex scripts and bidi (HarfBuzz-like or a small
  shaper)
- Font fallback chains (Cyrillic, CJK, emoji)
- Rich text spans in labels (bold, colour)
- IME support for Chinese, Japanese and Korean input
- Subpixel or gamma-correct anti-aliasing options, and a HiDPI scale
  factor

## Layout
- Horizontal and vertical boxes with spacing, padding and alignment
- A grid with any number of columns, row/column spans and weights
- Minimum and maximum sizes, plus anchors
- Constraint or flex layout
- Automatic HiDPI scaling (Kremlin hand-codes 1x–3x)

## Canvas
- Paths: polylines, polygons, Béziers, fills with even-odd/non-zero
  rules
- Line widths, dash patterns, caps and joins
- Transforms (translate, rotate, scale) and clipping
- Gradients and alpha blending
- Images on the canvas (PNG/JPEG through Omsk, not only PPM)
- Hit testing of shapes by id
- Layers and z-order
- An offscreen canvas exported to PNG (screenshots, Tula's export)

## Events and input
- Mouse drag events with button and modifier state, plus drag and drop
  (internal and from the OS)
- Double-click on all widgets (Kremlin has its own)
- Gamepad input via GLFW
- Keyboard focus traversal with Tab and Shift+Tab across all widgets
- Global shortcuts / accelerators table
- Clipboard API (`clipboard_get`, `clipboard_set`)
- Timers as first-class (`every(ms, "handler")`) next to `sleep`
- Window events: resize, move, minimise, close request with veto

## Architecture
- Retained widget tree inspection (`children`, `parent`) for tests
- Themes: colours, fonts and metrics in one style table, plus dark mode
- Accessibility: expose widgets to screen readers (UIA / AT-SPI)
- A software rasteriser backend for headless CI, with no GPU
- A Wayland backend check and a macOS (Cocoa/Metal or GL) build
- Moscow backend parity: the same API on Moscow's framebuffer
- A declarative UI description (UNO literal) loaded into widgets
- Reduce callback-process cost: optional shared-state callbacks that do
  not copy variables

## Tests
- Headless golden-image tests of every widget
- Event replay recordings for GUI regression tests
- Fill in TESTPLAN's ASCII-only gap with Unicode input tests
