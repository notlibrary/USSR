# Changelog

## 0.2.0
- Split: the document editor (`--pad`) is now Novosibirsk
  (`src/novosibirsk/novosibirsk.su`), which includes `siberia.su` for the
  shared parts. `siberia.su` is the spreadsheet only; the pad's engine
  checks moved to `tests/novosibirsk_test.su`.

## 0.1.0
- First version: a spreadsheet (default) and a document editor (`--pad`)
  in one program.
- Spreadsheet: 52 x 2000 cells, formulas with references, ranges,
  operators, comparisons and 19 functions, cycle detection, flat ODF
  (`.fods`) and CSV files.
- Pad: word-wrapped text in any installed TrueType font, size, bold and
  italic, flat ODF (`.fodt`) and plain-text files.
- Added `tests/siberia_test.su` (engine) and `tests/gui/siberia_test.su`
  (GUI).
