# Mbl — refinement ideas

**Today:** a spartan vi clone in one file (`Mbl.su`, ~1150 lines). Normal
mode has `h j k l`, arrows, `i a o x dd w cw`, counts, `u` / Ctrl+R with
unlimited history, and `:w :q :wq :q! :e`. Text is UTF-8 with one screen
column per character, and a BOM is kept. The screen is redrawn as one
string per frame through native raw-terminal primitives.

## Motions
- `b`, `e`, `ge`, plus `W B E` (WORD motions over non-blank runs)
- `0`, `^`, `$` (line start, first non-blank, line end)
- `gg`, `G`, `NG` and `:N` to go to a line (`Mbl_goto_line` already exists
  internally)
- `f F t T` with `;` and `,` to repeat
- `%` to jump to the matching bracket
- `H M L` for the top, middle and bottom of the screen
- `Ctrl+D Ctrl+U Ctrl+F Ctrl+B` to scroll by half or full pages; PgUp/PgDn
- `{` and `}` for paragraph motions
- `zz zt zb` to recentre the view on the cursor

## Operators and text objects
- A general `d`/`c`/`y` + motion grammar (`dw d$ c0 y3j`) in place of the
  special cases `dd` and `cw`
- `D C Y`, `s S`, `r` (replace one character), `R` (replace mode), `~`
  (toggle case)
- Text objects: `iw aw i" a" i( a( i[ a[ i{ a{ ip ap`
- `J` to join lines, `>>` and `<<` to indent, `=` to reindent
- `.` to repeat the last change, which fits the existing change grouping
- `p P` with an unnamed register, then named registers `"a`–`"z`
- A system clipboard register `"+` through a terminal OSC 52 sequence

## Visual mode
- `v`, `V` and `Ctrl+V` (block) selections with operators applied to them
- `gv` to reselect the last selection
- Show the selection with reverse-video ANSI attributes

## Search and replace
- `/pattern` and `?pattern` with `n` / `N`, wrapping at the file ends
- `*` and `#` to search for the word under the cursor
- Incremental search highlighting while the pattern is typed
- `:s/a/b/[g]` and `:%s/...` on top of the shell's regex engine
  (AGENTS.md: advanced regex patterns)
- `:g/pat/d`, `:v/pat/d`

## Ex commands
- Ranges: `:1,10d`, `:.,$w part.txt`, `:'<,'>`
- `:r file` to read a file in, `:w !cmd` / `:r !cmd` through the USSR shell
- `:set number`, `:set relativenumber`, `:set list`, `:set ts=4`,
  `:set expandtab`, `:set ignorecase smartcase`
- `:x` (write if changed, then quit), `:sav`, `:wa`
- `:help` showing the key summary from the file header
- Command-line history (Up/Down) and Tab completion of file names in `:e`

## Buffers and windows
- More than one buffer: `:e#`, `:bn`, `:bp`, `:ls`
- Split windows (`:sp`, `:vs`, `Ctrl+W hjkl`)
- Marks `ma` / `'a` / `` `a ``, a jump list (`Ctrl+O` / `Ctrl+I`)

## Display
- A line-number gutter, current-line highlight, a ruler with
  `line:col percent`
- Syntax highlighting for `.su` files, reusing the rules in
  `highlighters/`
- Show tabs and trailing blanks (`:set list`); soft wrap or horizontal
  scrolling for long lines
- Measure wide CJK and emoji characters (two columns) and zero-width
  combining marks
- A status line with mode, file name, `[+]`, encoding and line endings

## Files and safety
- Detect and keep CRLF line endings (`:set ff=dos|unix`)
- Swap or recovery files written every N changes; recover on start
- Write atomically through a temporary file and a rename
- A warning when the file changed on disk since it was loaded
- Persistent undo across sessions (`~/.mbl/undo/`)
- Open a file at a line: `Mbl.su +42 file`

## Configuration and extension
- An `~/.mblrc` of ex commands run at start
- Key remapping (`:map`, `:nmap`, `:imap`), in line with the shell's
  keyboard (re)binding goal
- Abbreviations (`:ab`)
- A macro recorder: `qa ... q`, `@a`, `@@`
- A hook to run a USSR function on a key, so Mbl can be scripted in USSR

## Insert mode
- Auto-indent: a new line copies the previous line's indentation
- `Ctrl+W` to delete a word, `Ctrl+U` to delete to the line start
- Word completion from the buffer (`Ctrl+N` / `Ctrl+P`)
- Bracket and quote pairing (optional)

## Tests and tooling
- A headless key-script test mode (`Mbl.su --keys "3dd:wq" file`) for
  `tests/mbl/`
- Benchmarks for redrawing large files; redraw only the lines that changed
