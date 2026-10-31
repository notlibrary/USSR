# Siberia

Siberia is a small spreadsheet written in USSR on a Petersburg canvas.
Start it from the repository root with a GLFW-enabled build:

    ussr src/siberia/siberia.su                  # empty sheet
    ussr src/siberia/siberia.su budget.fods      # open or start a sheet

The document editor that used to be `siberia.su --pad` is now its own
program, [Novosibirsk](../novosibirsk/README.md).

## Files

Siberia keeps sheets in OpenDocument's flat, single-XML form (`.fods`),
which LibreOffice, Calligra and other ODF programs open and save directly.
It also reads and writes `.csv` (values; `;` separated files are
detected). Zip-packed `.ods` (and `.xlsx`) are refused with a message;
save them as flat ODF in LibreOffice first. A name without an extension
gets `.fods`.

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
