# Siberia

Siberia is a small spreadsheet written in USSR on a Petersburg canvas.
Start it from the repository root with a GLFW-enabled build:

    ussr src/siberia/siberia.su                  # empty sheet
    ussr src/siberia/siberia.su budget.fods      # open or start a sheet

The document editor that used to be `siberia.su --pad` is now its own
program, [Novosibirsk](../novosibirsk/README.md).

## Files

Siberia opens and saves OpenDocument spreadsheets, both formats:
- `.ods`, the usual zipped package that LibreOffice saves.
- `.fods`, the flat, single-XML form.

LibreOffice, Calligra and other ODF programs open both. All the sheets and
their formulas are kept; styles are not.

It also reads and writes `.csv`. Formulas are saved as their values, and
files separated by `;` are detected.

`.xlsx` is refused with a message; save it as `.ods` in LibreOffice first.
A name without an extension gets `.fods` (the file dialog's Spreadsheets
filter adds `.ods`).

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

A drag with the mouse, Shift with the arrows or a Shift+click selects
cells; the status bar shows their Sum, Average and Count.

The menu row is above compact File and Spreadsheet toolbars, which start side
by side. Use **TOOLS** to show or hide either one; hover over a button for its
name, or drag a dotted grip horizontally to reorder and vertically to move a
toolbar to another row.

## Sheets

A workbook has one or more sheets, shown as tabs under the grid.
- **Switching:** click a tab, or use Ctrl+Page Down and Ctrl+Page Up.
- **Adding:** `+` (or Shift+F11) adds a sheet.
- **Renaming:** a double click on a tab renames it.
- **Deleting:** a right click on a tab deletes it, after asking. The last
  sheet stays.

Formulas reach another sheet's cells as `Sheet2.A1` or `Sheet2!A1`. A name
with spaces is quoted: `'My data'.A1`. Ranges work too:
`SUM(Sheet2.A1:B9)`. Renaming a sheet rewrites the formulas that name it.
A deleted or unknown sheet is `#REF!`.

A `.fods` file keeps every sheet. CSV holds only the current one.

Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+Q quit,
F1 help. New and Open ask before unsaved changes are lost.

## Implementation

All of Siberia is USSR code in `siberia.su`. The engine (cells and formula
evaluation, the ODF and CSV readers and writers) works without a window
and is what `tests/siberia_test.su` checks; `tests/gui/siberia_test.su`
drives the sheet and the Novosibirsk pad windows.

`siberia.su` also holds what Novosibirsk shares with it: the small
helpers, the XML reader, the file dialog, the status bar and the window
event handlers. Novosibirsk defines `SB_PAD` and includes `siberia.su`;
the handlers' pad branches sit under `$ifdef SB_PAD`, so the spreadsheet
alone carries no editor code. The started file defines `SB_APP`, which
brings in the window callbacks and `sb_app_main`.
