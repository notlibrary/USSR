# Changelog

## 0.3.0
Refine pass 1 from REFINE.md; the new code is `more.su`.
- About 110 more functions: VLOOKUP HLOOKUP INDEX MATCH XLOOKUP; SUMIF(S)
  COUNTIF(S) AVERAGEIF(S) with `>5`, `<>x` and `*`/`?` criteria; LEFT
  RIGHT MID FIND SEARCH SUBSTITUTE TRIM CONCAT TEXTJOIN TEXT VALUE REPT
  EXACT PROPER CHAR CODE; TODAY NOW DATE TIME YEAR MONTH DAY HOUR MINUTE
  WEEKDAY DATEDIF EDATE EOMONTH (serial days as LibreOffice); SUMPRODUCT
  PRODUCT ROUNDUP ROUNDDOWN CEILING FLOOR TRUNC SIGN LOG LOG10 LN EXP
  RAND RANDBETWEEN trigonometry FACT GCD LCM; MEDIAN MODE STDEV STDEVP
  VAR VARP LARGE SMALL RANK PERCENTILE QUARTILE COUNTA COUNTBLANK;
  IFERROR IFNA IFS SWITCH XOR IS...; PMT FV PV NPV IRR; SLOPE INTERCEPT
  CORREL FORECAST TREND; BIT... and base conversions; SUBTOTAL; and
  `USSR("definition", ...)` calling USSR code.
- Named ranges (`sbm_define`), used by name in formulas.
- Ctrl+Z and Ctrl+Y undo and redo typed cells; Ctrl+D copies the cell
  above with its references moved.
- Sheet tools: sort by several columns, find and replace, fill series,
  insert and delete rows and columns with every reference following,
  remove duplicates, transpose, text to columns, goal seek, an error
  checker, precedents and dependents, scenarios, range statistics.
- Number formats in TEXT(); CSV with a delimiter, quote, header row, BOM
  and decimal comma; Windows-1251; HTML and Markdown tables.
- `ussr src/siberia/siberia.su --eval file CELL...` and `--convert in
  out` (.fods, .csv, .html, .md) need no window.
- `tests/siberia_more_test.su` (145 checks, a formula fuzz test among
  them); `tests/siberia_test.su` and Novosibirsk's test are unchanged.

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
