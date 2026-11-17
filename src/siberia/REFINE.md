# Siberia — refinement ideas

**Today:** a spreadsheet on a Petersburg canvas (0.2.0) with 52 columns
(A..AZ) by 2000 rows. Formulas have references, absolute refs, ranges,
arithmetic, `%`, `&` and comparisons. There are 19 functions (SUM
AVERAGE MIN MAX COUNT ABS INT ROUND SQRT MOD POWER PI IF AND OR NOT LEN
UPPER LOWER), the error values and cycle detection. Files are flat ODF
`.fods` and CSV. It also holds code shared with Novosibirsk.

**Refine pass 1 (0.3.0):** `more.su`; [x] done, [~] partly done. See
CHANGELOG.md and `tests/siberia_more_test.su`.

## Grid
- More columns (AA..XFD) and rows, with a sparse cell store
- Several sheets with tabs, and cross-sheet references (`Sheet2.A1`)
- Column widths and row heights, resizable by drag, plus autofit
- Freeze panes, and hide or unhide rows and columns
- [~] Insert and delete rows and columns, with references adjusted — partly: sbm_insert inserts and deletes rows and columns and moves every reference, as a library call in more.su; no key or dialog yet
- Merge cells
- Range selection with Shift/mouse, then copy, cut and paste (TSV on the
  clipboard)
- [~] Fill handle and Ctrl+D/Ctrl+R fill, with series detection (1, 2, 3...;
  Mon, Tue...) — partly: Ctrl+D copies the cell above (formulas follow); sbm_fill fills series (numbers, weekdays, months, Item1...), as a library call in more.su; no key or dialog yet
- [x] Undo/redo
- [~] Find and replace — partly: sbm_find and sbm_replace, as a library call in more.su; no key or dialog yet
- [~] Sort a range by one or more columns, and AutoFilter — partly: sbm_sort by several columns, as a library call in more.su; no key or dialog yet; no AutoFilter

## Formulas
- [x] Lookup: VLOOKUP, HLOOKUP, INDEX, MATCH, XLOOKUP
- [x] Conditional aggregates: SUMIF(S), COUNTIF(S), AVERAGEIF(S)
- [x] Text: LEFT, RIGHT, MID, FIND, SEARCH, SUBSTITUTE, TRIM, CONCAT,
  TEXTJOIN, TEXT, VALUE, REPT
- [x] Dates: TODAY, NOW, DATE, YEAR, MONTH, DAY, WEEKDAY, DATEDIF, EDATE,
  date serial numbers
- [x] Maths: SUMPRODUCT, PRODUCT, ROUNDUP, ROUNDDOWN, CEILING, FLOOR, LOG,
  LN, EXP, RAND, RANDBETWEEN, trigonometry
- [x] Statistics: MEDIAN, MODE, STDEV, VAR, LARGE, SMALL, RANK, PERCENTILE
- [x] Logic: IFERROR, IFS, SWITCH, XOR, ISBLANK, ISNUMBER, ISTEXT
- [x] Finance: PMT, FV, PV, NPV, IRR
- [~] Named ranges — partly: sbm_define names a cell or a range and formulas use it; no dialog, not saved in .fods
- Array formulas / spilled ranges
- Incremental recalculation through a dependency graph, not full
  re-evaluation
- R1C1 notation in ODF import (`of:=[.A1]`), checked for every function
- Formula autocomplete and argument hints while typing

## Formatting
- [~] Number formats: decimals, thousands separator, percent, currency,
  date, time, scientific — partly: TEXT(value, format) with decimals, thousands, percent, currency, scientific, dates and times; cells keep no format yet
- Bold, italic, font colour, fill colour, borders, alignment, wrap
- Conditional formatting (colour scales, rules)
- Styles kept in `.fods` automatic styles

## Data
- Charts: bar, line, pie and scatter on the canvas, saved as ODF charts
- Data validation (lists, number ranges) with drop-downs
- Pivot tables
- [~] CSV import options: delimiter, encoding, quote and header row — partly: sbm_csv_load: delimiter, quote, header row, a BOM, decimal comma, as a library call in more.su; no key or dialog yet
- XLSX and ODS (zip) read/write once a zip library exists
- Database query cells through Dubna (`=SQL("select ...")`) as an
  opt-in
- [~] Export to HTML, Markdown tables and PDF — partly: sbm_html and sbm_markdown (and --convert); no PDF

## Interface
- Formula bar showing the cell's typed text
- Name box (go to cell `B300`)
- Zoom
- [~] Status bar with SUM/AVERAGE/COUNT of the selection — partly: sbm_stats gives Sum, Average and Count of a range; the status bar does not show it yet
- Comments on cells
- Keyboard shortcuts close to LibreOffice's

## Engine and tests
- [x] A headless CLI: `siberia.su --eval file.fods Sheet1.B5` and
  `--convert in.csv out.fods`
- Tests that compare results with LibreOffice for every function
- [x] Fuzz the formula parser

## Analysis tools
- [~] Goal seek and a simple solver (one target cell, several inputs) — partly: sbm_goal_seek (secant method); no solver for several inputs
- [x] What-if scenarios: named sets of input values
- [~] Trace precedents and dependents with arrows; show formulas mode — partly: sbm_precedents and sbm_dependents list the cells; no arrows
- [x] An error checker listing every cell with `#...!` and why
- [~] Subtotals and grouping (outline) of rows and columns — partly: SUBTOTAL(1..11 and 101..111); no grouping
- [x] Remove duplicates, text to columns, transpose on paste
- Paste special: values only, formats only, operations
- Sparklines in a cell
- [x] Regression and moving-average functions (`TREND`, `FORECAST`,
  `SLOPE`, `INTERCEPT`, `CORREL`)
- [~] Bit and base functions (`BITAND`, `DEC2HEX`, `HEX2DEC`), sharing
  Klin's engine — partly: BITAND BITOR BITXOR BITLSHIFT BITRSHIFT DEC2HEX HEX2DEC DEC2BIN BIN2DEC DEC2OCT OCT2DEC, in more.su (not Klin's code)

## Sheets as programs
- [x] User functions written in USSR and called from cells
  (`=USSR("name", A1)`)
- Macros: record and replay actions as a USSR script
- Web and file functions: `=IMPORTCSV(path)`, `=HTTPGET(url)` (opt-in)
- KGB link: `=KGB("cat", "legs")` reads a property from a knowledge
  base
- Cell protection and locked sheets
- Print layout: page breaks, repeat header rows, fit to width

## Shared code
- Move the parts Novosibirsk includes (XML, file dialog, status bar,
  event handlers) into a common file or into Petersburg and the
  standard library, so neither program includes the other
- Keep unknown `.fods` content on round trip (styles, charts, settings
  from LibreOffice written back untouched)
- Very large CSV files: load in chunks with a progress line
- [~] Text encoding choice for CSV (UTF-8, Windows-1251) and a BOM option — partly: sbm_from_cp1251 and sbm_to_cp1251, and a BOM option in sbm_csv_text; no dialog
- [~] Locale: decimal comma and `;` separators on input and output, date
  formats — partly: a decimal comma and any delimiter in sbm_csv_load and sbm_csv_text; no date formats by locale
