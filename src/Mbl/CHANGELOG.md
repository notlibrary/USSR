# Mbl changelog

## 0.2.0

Refine pass 1 from REFINE.md; the new code is `more.su`, which `Mbl.su`
includes and calls from its key reader, normal and insert modes, ex
commands, file I/O and screen.

- Motions: `b e W B E ge 0 ^ $ gg G NG f F t T ; , % { } H M L`, with
  counts; `Ctrl+D Ctrl+U Ctrl+F Ctrl+B` and `zz zt zb` scroll.
- A general operator grammar: `d c y g~ gu gU > <` with any motion, with
  text objects (`iw aw iW aW i" a" i' a' i( a( ib i[ a[ i{ a{ iB i< a<`)
  and in doubled form (`dd cc yy >> << g~~ guu gUU`); counts before and
  after the operator multiply. `cw` keeps Mbl's own rules.
- `D C Y s S x X r ~ J gJ O I A p P`, unnamed and named registers
  (`"a`–`"z`; `x` and every delete fill them).
- `.` repeats the last change (keys and typed text), `q{a-z}` records a
  macro, `@{a-z}` and `@@` play it with counts; both run through a key
  queue in `Mbl_next`.
- Marks `m{a-z}`, `'a`, `` `a ``, and `''` after a jump.
- Search: `/` and `?` with regular expressions, `n N`, `* #`, wrapping
  (`:set nows` stops at the ends), `:set ic`.
- Ex commands: `:N`, `:x`, `:sav F`, `:r F`, ranges (`N,M . $ % 'a`
  with `+N`/`-N`) for `d y > < j`, `:s/pat/rep/[g][i]` with groups and
  `&`, `:g/pat/d`, `:v/pat/d`, `:sort [n] [u] [r]`, `:set` (`nu ai et ic
  ws ts= sw= ff=dos|unix ro`), `:reg`, `:marks`, `:noh`.
- `Ctrl+A` / `Ctrl+X` add to the number at or after the cursor.
- Insert mode: Tab (spaces with `:set et`), `Ctrl+W`, `Ctrl+U`, and
  auto-indent with `:set ai`.
- Files: CRLF line ends are detected and kept (`:set ff=` converts),
  writes go through a temporary file, `Mbl.su +N file` opens at a line,
  `-R` and `:set ro` refuse writes.
- Screen: a line-number gutter (`:set nu`) and a ruler with
  `line:col percent`, `dos`, `[RO]` and the recording register.
- `tests/mbl/run_mbl_tests.sh` gained 77 cases (154 in all).

## 0.1.0

- A vi clone: `h j k l`, arrows, `i a o x dd w cw`, counts, undo and redo
  with unlimited history, `:w :q :wq :q! :e`, UTF-8 text with a kept BOM.
