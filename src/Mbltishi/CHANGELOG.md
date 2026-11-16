# Mbltishi changelog

## 0.2.0

Refine pass 1 from REFINE.md.

- `vt.su`: a VT100/xterm emulator model in USSR that runs headless on
  every system. It keeps a grid of cells (character, colours,
  attributes, hyperlink, width) and carries out C0 controls, ESC 7/8/D/E/
  M/c, CSI cursor moves, erasing, insert/delete of characters and lines,
  scroll regions, the alternate screen, save/restore, SGR with 16, 256 and
  24-bit colours, private modes (cursor keys, autowrap, cursor, mouse
  1000/1002/1003/1006, bracketed paste), cursor shapes, OSC 0/2/7/8/52/
  133/9;4, and answers DSR, CPR and DA. Wide CJK and emoji take two cells;
  combining marks join their letter; a sequence split between writes
  waits for the rest.
- A scrollback with search and save, resize, key/paste/mouse encoding
  (`vt_key`, `vt_paste`, `vt_mouse`), URL and `path:line` detection,
  asciicast v2 recording and replay, `~/.mbltishi` reading and four
  colour schemes.
- The launcher takes `--headless FILE [ROWS COLS]` (prints the screen
  the file draws, on any system), `--cwd`, `--env NAME=VALUE`, `--hold`
  and `--scheme`.
- `tests/mbltishi_vt_test.su`: 62 golden checks, under ussr and
  russia.su.

## 0.1.0

- The launcher: a Petersburg window with Moscow's terminal running the
  USSR shell or another program.
