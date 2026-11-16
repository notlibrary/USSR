# Mbl — refinement ideas

**Today:** a spartan vi clone in one file (`Mbl.su`, ~1150 lines). Normal
mode has `h j k l`, arrows, `i a o x dd w cw`, counts, `u` / Ctrl+R with
unlimited history, and `:w :q :wq :q! :e`. Text is UTF-8 with one screen
column per character, and a BOM is kept. The screen is redrawn as one
string per frame through native raw-terminal primitives.

**Refine pass 1 (2026-11-16):** [x] done, [~] partly done; see
CHANGELOG.md 0.2.0. The new code is more.su.

## Motions
- [x] `b`, `e`, `ge`, plus `W B E` (WORD motions over non-blank runs)
- [x] `0`, `^`, `$` (line start, first non-blank, line end)
- [x] `gg`, `G`, `NG` and `:N` to go to a line (`Mbl_goto_line` already exists
  internally)
- [x] `f F t T` with `;` and `,` to repeat
- [x] `%` to jump to the matching bracket
- [x] `H M L` for the top, middle and bottom of the screen
- [x] `Ctrl+D Ctrl+U Ctrl+F Ctrl+B` to scroll by half or full pages; PgUp/PgDn
- [x] `{` and `}` for paragraph motions
- [x] `zz zt zb` to recentre the view on the cursor

## Operators and text objects
- [x] A general `d`/`c`/`y` + motion grammar (`dw d$ c0 y3j`) in place of the
  special cases `dd` and `cw`
- [x] `D C Y`, `s S`, `r` (replace one character), `R` (replace mode), `~`
  (toggle case)
- [x] Text objects: `iw aw i" a" i( a( i[ a[ i{ a{ ip ap`
- [x] `J` to join lines, `>>` and `<<` to indent, `=` to reindent
- [x] `.` to repeat the last change, which fits the existing change grouping
- [~] `p P` with an unnamed register, then named registers `"a`–`"z` — partly: unnamed and named registers "a-"z for d/c/y/x/p/P; no clipboard register
- A system clipboard register `"+` through a terminal OSC 52 sequence

## Visual mode
- `v`, `V` and `Ctrl+V` (block) selections with operators applied to them
- `gv` to reselect the last selection
- Show the selection with reverse-video ANSI attributes

## Search and replace
- [x] `/pattern` and `?pattern` with `n` / `N`, wrapping at the file ends
- [x] `*` and `#` to search for the word under the cursor
- Incremental search highlighting while the pattern is typed
- [x] `:s/a/b/[g]` and `:%s/...` on top of the shell's regex engine
  (AGENTS.md: advanced regex patterns)
- [x] `:g/pat/d`, `:v/pat/d`

## Ex commands
- [~] Ranges: `:1,10d`, `:.,$w part.txt`, `:'<,'>` — partly: N, N,M, ., $, %, 'a and +/- offsets for d, y, >, <, j, s, g, v, sort; no :w of a range
- [~] `:r file` to read a file in, `:w !cmd` / `:r !cmd` through the USSR shell — partly: :r file; no shell filters
- [~] `:set number`, `:set relativenumber`, `:set list`, `:set ts=4`,
  `:set expandtab`, `:set ignorecase smartcase` — partly: :set nu, ai, et, ic, ws, ts, sw, ff, ro and their no- forms; no relativenumber or list
- [~] `:x` (write if changed, then quit), `:sav`, `:wa` — partly: :x and :sav; no :wa (one buffer)
- `:help` showing the key summary from the file header
- Command-line history (Up/Down) and Tab completion of file names in `:e`

## Buffers and windows
- More than one buffer: `:e#`, `:bn`, `:bp`, `:ls`
- Split windows (`:sp`, `:vs`, `Ctrl+W hjkl`)
- [~] Marks `ma` / `'a` / `` `a ``, a jump list (`Ctrl+O` / `Ctrl+I`) — partly: marks m{a-z}, 'a and `a, and '' after jumps as a mark; no Ctrl+O jump list

## Display
- [~] A line-number gutter, current-line highlight, a ruler with
  `line:col percent` — partly: a line-number gutter (:set nu) and a line:col percent ruler; no current-line highlight
- Syntax highlighting for `.su` files, reusing the rules in
  `highlighters/`
- Show tabs and trailing blanks (`:set list`); soft wrap or horizontal
  scrolling for long lines
- Measure wide CJK and emoji characters (two columns) and zero-width
  combining marks
- [~] A status line with mode, file name, `[+]`, encoding and line endings — partly: the ruler shows line:col, percent, dos line ends, [RO] and a recording macro

## Files and safety
- [x] Detect and keep CRLF line endings (`:set ff=dos|unix`)
- Swap or recovery files written every N changes; recover on start
- [x] Write atomically through a temporary file and a rename
- A warning when the file changed on disk since it was loaded
- Persistent undo across sessions (`~/.mbl/undo/`)
- [x] Open a file at a line: `Mbl.su +42 file`

## Configuration and extension
- An `~/.mblrc` of ex commands run at start
- Key remapping (`:map`, `:nmap`, `:imap`), in line with the shell's
  keyboard (re)binding goal
- Abbreviations (`:ab`)
- [x] A macro recorder: `qa ... q`, `@a`, `@@`
- A hook to run a USSR function on a key, so Mbl can be scripted in USSR

## Insert mode
- [x] Auto-indent: a new line copies the previous line's indentation
- [x] `Ctrl+W` to delete a word, `Ctrl+U` to delete to the line start
- Word completion from the buffer (`Ctrl+N` / `Ctrl+P`)
- Bracket and quote pairing (optional)

## Tests and tooling
- A headless key-script test mode (`Mbl.su --keys "3dd:wq" file`) for
  `tests/mbl/`
- Benchmarks for redrawing large files; redraw only the lines that changed

## Numbers, text and code helpers
- [x] `Ctrl+A` / `Ctrl+X` to add to or subtract from the number under the
  cursor, with counts
- [~] `gu`, `gU` and `g~` case operators, `gJ` (join without a space) — partly: gu, gU and g~ with motions, text objects and doubled forms, gJ; no other g commands
- `gq` to reflow a paragraph to `textwidth`, and automatic wrapping while
  typing
- [~] `:sort [n] [u]` on a range, `:retab`, and trimming trailing blanks on
  write — partly: :sort with n, u and r on a range; no :retab
- `ga` to show the code point and UTF-8 bytes of the character under
  the cursor
- Digraphs (`Ctrl+K`) for characters the keyboard lacks («», —, №)
- `gf` to open the file name under the cursor, and `$include` paths in
  `.su` files
- `K` to show the header-comment contract of the USSR builtin or
  function under the cursor
- `:make`-style run of `ussr %` with a quickfix list of
  `file:line: reason` errors (`:cn`, `:cp`)
- Comment toggling for `#` lines (`gc`)
- Folding of `[ ... ]` blocks (`zo zc za zR zM`)
- Tags for definitions: jump to a function definition (`Ctrl+]`) and
  back (`Ctrl+T`), built from a scan of `name(r): ... [` lines

## Robustness
- Large files: load lazily or by chunks, so a 100 MB log opens at once
- A binary-file guard (NUL bytes) with a hex mode (`:hex`)
- Other encodings: detect and convert Windows-1251, KOI8-R and UTF-16,
  keeping the original on write
- Terminal resize handled while running (rows and columns are read at
  start by `Mbl_termsize`)
- [~] A read-only mode (`-R`, `:set ro`) and a view-only pager mode — partly: -R and :set ro refuse writes; no pager mode
- Restore the terminal on every exit path, including runtime errors
- Mouse support under terminals that report it: click to place the
  cursor, wheel to scroll
