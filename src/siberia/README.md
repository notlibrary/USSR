# Siberia

Siberia is a small office package written in USSR on a Petersburg canvas: a
spreadsheet by default, a document editor with `--pad`. Start it from the
repository root with a GLFW-enabled build:

    ussr src/siberia/siberia.su                  # spreadsheet
    ussr src/siberia/siberia.su budget.fods      # open or start a sheet
    ussr src/siberia/siberia.su --pad            # document editor
    ussr src/siberia/siberia.su --pad notes.fodt

## Files

Siberia keeps files in OpenDocument's flat, single-XML form, which
LibreOffice, Calligra and other ODF programs open and save directly:

| Program     | Native   | Also            |
|-------------|----------|-----------------|
| Spreadsheet | `.fods`  | `.csv` (values; `;` separated files are detected) |
| Pad         | `.fodt`  | `.txt`          |

Zip-packed `.ods`/`.odt` (and `.xlsx`/`.docx`) are refused with a message;
save them as flat ODF in LibreOffice first. A name without an extension
gets the native one.

## Spreadsheet

52 columns (A..AZ) by 2000 rows. Type to replace a cell, F2 or a double
click to change it, Enter/Tab to keep it and move, Esc to cancel, Delete to
clear. Arrows, Page Up/Down, Home, Ctrl+Home and Ctrl+End move; the wheel
scrolls.

Formulas start with `=`: numbers, `"text"`, references (`A1`, `$A$1`),
ranges (`A1:B5`) in function arguments, `+ - * / ^`, unary minus, `%`,
`&` (join text), and `= <> < > <= >=` (text compares without regard to
case). Functions: `SUM AVERAGE MIN MAX COUNT` (ranges skip text and empty
cells), `ABS INT ROUND SQRT MOD POWER PI`, `IF AND OR NOT`, `LEN UPPER
LOWER`; arguments are separated by `,` or `;`. Errors show in red:
`#DIV/0!`, `#VALUE!`, `#NAME?`, `#REF!`, `#NUM!`, `#ERROR!` (syntax) and
`#CYCLE!` (a cell that depends on itself). Numbers show up to 10
significant digits.

## Pad

Type to write; any script works (input arrives through Petersburg's
`"char"` event). Arrows, Home/End (of the wrapped line), Ctrl+Home/End,
Page Up/Down, Enter, Backspace, Delete; Tab inserts four spaces; a click
places the cursor. **FONT** lists the TrueType families installed on the
system (Windows `Fonts` folders, `/usr/share/fonts`, `~/.fonts`, macOS
font folders); **-**/**+** step the size through 8..72 pt; **B** and **I**
(Ctrl+B, Ctrl+I) pick the bold and italic faces. Font, size and style
apply to the whole document and are saved as its default paragraph style.

Both programs: Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save
as, Ctrl+Q quit, F1 help. New and Open ask before unsaved changes are lost.

## Implementation

All of Siberia is USSR code in `siberia.su`. The engine (cells and formula
evaluation, the ODF and CSV readers and writers, the document and its
cursor) works without a window and is what `tests/siberia_test.su` checks;
`tests/gui/siberia_test.su` drives both windows. Outline text comes from
Petersburg's `loadfont`/`canvasfonttext`, a small TrueType reader in
`src/ttf.h`.
