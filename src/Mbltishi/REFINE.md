# Mbltishi — refinement ideas

**Today:** a 72-line launcher that opens a Petersburg canvas window and
runs the USSR shell, or another program, in Moscow's own terminal
(`terminal_open` / `terminal_run`): a grid in Pobeda that handles ANSI
cursor moves, erasing and colours. The window closes when the program
ends. On Windows and Linux it hands the job to the system's terminal.

**Refine pass 1 (2026-11-16):** [x] done, [~] partly done; see
CHANGELOG.md 0.2.0. The emulator items are done in the vt.su model;
Moscow's window terminal (lib/gui/term.c) is unchanged.

## Terminal emulation
- [~] Fuller VT100/xterm coverage: scroll regions (DECSTBM), insert and delete
  line or character, the alternate screen buffer (`?1049h`), save and
  restore the cursor — partly: in the vt.su model (DECSTBM, IL/DL, ICH/DCH/ECH, alternate screen, save/restore); Moscow's term.c draws the window as before
- [~] 256-colour and 24-bit (truecolor) SGR, bold/dim/italic/underline/blink/
  reverse attributes — partly: in the model: 16, 256 and truecolor, the attribute flags
- [~] Wide-character (CJK) cells and combining marks — partly: in the model: wide cells and combining marks
- [~] Bracketed paste mode (`?2004h`) — partly: in the model: the mode and vt_paste
- [~] Mouse reporting (X10/SGR modes), so Mbl and the other TUIs can take
  clicks — partly: in the model: modes 1000/1002/1003 and 1006, vt_mouse encodes X10 and SGR reports
- [~] OSC 0/2 window titles, OSC 52 clipboard, OSC 8 hyperlinks — partly: in the model: OSC 0/2, 52 (set and query) and 8 links on cells
- [~] Cursor shapes (block, bar, underline) and blink, DECSCUSR — partly: in the model: DECSCUSR shapes kept for the renderer
- [~] Answers to device status reports (DSR/CPR) and DA queries — partly: in the model: DSR 5/6, primary and secondary DA replies

## Scrollback and selection
- [~] A scrollback buffer with mouse-wheel and Shift+PgUp/PgDn scrolling — partly: the model keeps a scrollback (vt_scrollback); no window scrolling yet
- Mouse text selection and copy, plus paste with middle click or
  Ctrl+Shift+V
- [~] Search in the scrollback (Ctrl+Shift+F) — partly: vt_search over the scrollback and screen; no key binding
- [x] Save the scrollback to a file

## Windows and tabs
- Tabs: several sessions in one window, Ctrl+Shift+T / Ctrl+Tab
- Split panes, horizontal and vertical
- [~] Resizing the window resizes the grid and tells the program
  (SIGWINCH-like event) — partly: vt_resize keeps the text; no window events
- Full screen (F11) and a zoom level (Ctrl+= / Ctrl+-)

## Configuration
- [~] A `~/.mbltishi` file with the font, size, colour scheme, scrollback
  length, cursor and bell — partly: ~/.mbltishi is read (vt_config); the window takes the scheme option, not yet the font
- [~] Built-in colour schemes that match Kremlin's wallpapers (Steppe,
  Graphite, ...) — partly: Graphite, Steppe, Kremlin and Mono schemes (vt_scheme)
- Choose any installed TrueType font through Petersburg's `loadfont`
- A visual bell, or no bell at all

## Platform reach
- Run on Windows and Linux too, through a pseudo-console (ConPTY,
  forkpty), not only in Moscow
- [x] Start with a working directory and environment: `Mbltishi --cwd DIR
  --env K=V`
- [~] `--hold`: keep the window open after the program exits, showing its
  exit status — partly: the window stays until it is closed; no exit status shown

## Integration
- A Kremlin launcher argument to open a new tab in a running Mbltishi
- Drag a file from Kremlin or Tver to paste its path
- [~] Clickable `file:line` references that open in Mbl — partly: vt_file_refs finds path:line; no click handling
- Notify Kremlin's message area when a long command finishes in a
  background window

## Tests
- [x] A headless mode that feeds an escape-sequence script and dumps the
  grid, for golden-file tests of the emulator
- vttest-style conformance checks

## Shell integration
- [~] Shell integration marks (OSC 133): jump between prompts, select the
  output of one command, and show its exit status in the margin — partly: OSC 133 A marks are recorded (prompts); no jumping
- [~] Report the current directory (OSC 7), so a new tab opens in it — partly: OSC 7 is parsed into cwd
- A right-click menu: copy, paste, clear, new tab, settings
- Paste safety: ask before pasting several lines into the shell
- [~] Progress reporting (OSC 9;4) shown on Kremlin's panel button — partly: OSC 9;4 is parsed into progress; Kremlin does not show it yet

## Sessions
- Detach and reattach: keep the program running when the window closes
  (`--detach`, `--attach NAME`)
- Named profiles: a program, a directory, colours and a font per
  profile, listed in the new-tab menu
- [~] Record a session with timing (asciinema-style) and replay it — partly: vt_record, vt_cast (asciicast v2) and vt_replay
- Broadcast input to all panes
- Log all output to a file (`--log FILE`)

## Graphics and text
- Inline images (Sixel or the Kitty protocol) so Omsk output can be
  shown in the terminal
- Ligatures and bold or italic faces once Pobeda has them
- Line-drawing through the font's box glyphs with pixel-exact joins at
  every zoom
- Background transparency or a dimmed unfocused window
- [~] URL detection without OSC 8: Ctrl+click opens a link — partly: vt_urls finds links; no Ctrl+click
