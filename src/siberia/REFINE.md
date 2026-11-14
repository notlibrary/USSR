# Siberia — refinement ideas

**Today:** a spreadsheet on a Petersburg canvas (0.2.0) with 52 columns
(A..AZ) by 2000 rows. Formulas have references, absolute refs, ranges,
arithmetic, `%`, `&` and comparisons. There are 19 functions (SUM
AVERAGE MIN MAX COUNT ABS INT ROUND SQRT MOD POWER PI IF AND OR NOT LEN
UPPER LOWER), the error values and cycle detection. Files are flat ODF
`.fods` and CSV. It also holds code shared with Novosibirsk.

## Grid
- More columns (AA..XFD) and rows, with a sparse cell store
- Several sheets with tabs, and cross-sheet references (`Sheet2.A1`)
- Column widths and row heights, resizable by drag, plus autofit
- Freeze panes, and hide or unhide rows and columns
- Insert and delete rows and columns, with references adjusted
- Merge cells
- Range selection with Shift/mouse, then copy, cut and paste (TSV on the
  clipboard)
- Fill handle and Ctrl+D/Ctrl+R fill, with series detection (1, 2, 3...;
  Mon, Tue...)
- Undo/redo
- Find and replace
- Sort a range by one or more columns, and AutoFilter

## Formulas
- Lookup: VLOOKUP, HLOOKUP, INDEX, MATCH, XLOOKUP
- Conditional aggregates: SUMIF(S), COUNTIF(S), AVERAGEIF(S)
- Text: LEFT, RIGHT, MID, FIND, SEARCH, SUBSTITUTE, TRIM, CONCAT,
  TEXTJOIN, TEXT, VALUE, REPT
- Dates: TODAY, NOW, DATE, YEAR, MONTH, DAY, WEEKDAY, DATEDIF, EDATE,
  date serial numbers
- Maths: SUMPRODUCT, PRODUCT, ROUNDUP, ROUNDDOWN, CEILING, FLOOR, LOG,
  LN, EXP, RAND, RANDBETWEEN, trigonometry
- Statistics: MEDIAN, MODE, STDEV, VAR, LARGE, SMALL, RANK, PERCENTILE
- Logic: IFERROR, IFS, SWITCH, XOR, ISBLANK, ISNUMBER, ISTEXT
- Finance: PMT, FV, PV, NPV, IRR
- Named ranges
- Array formulas / spilled ranges
- Incremental recalculation through a dependency graph, not full
  re-evaluation
- R1C1 notation in ODF import (`of:=[.A1]`), checked for every function
- Formula autocomplete and argument hints while typing

## Formatting
- Number formats: decimals, thousands separator, percent, currency,
  date, time, scientific
- Bold, italic, font colour, fill colour, borders, alignment, wrap
- Conditional formatting (colour scales, rules)
- Styles kept in `.fods` automatic styles

## Data
- Charts: bar, line, pie and scatter on the canvas, saved as ODF charts
- Data validation (lists, number ranges) with drop-downs
- Pivot tables
- CSV import options: delimiter, encoding, quote and header row
- XLSX and ODS (zip) read/write once a zip library exists
- Database query cells through Dubna (`=SQL("select ...")`) as an
  opt-in
- Export to HTML, Markdown tables and PDF

## Interface
- Formula bar showing the cell's typed text
- Name box (go to cell `B300`)
- Zoom
- Status bar with SUM/AVERAGE/COUNT of the selection
- Comments on cells
- Keyboard shortcuts close to LibreOffice's

## Engine and tests
- A headless CLI: `siberia.su --eval file.fods Sheet1.B5` and
  `--convert in.csv out.fods`
- Tests that compare results with LibreOffice for every function
- Fuzz the formula parser
