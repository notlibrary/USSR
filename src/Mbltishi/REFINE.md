# Mbltishi — refinement ideas

**Today:** a 72-line launcher that opens a Petersburg canvas window and
runs the USSR shell, or another program, in Moscow's own terminal
(`terminal_open` / `terminal_run`): a grid in Pobeda that handles ANSI
cursor moves, erasing and colours. The window closes when the program
ends. On Windows and Linux it hands the job to the system's terminal.

## Terminal emulation
- Fuller VT100/xterm coverage: scroll regions (DECSTBM), insert and delete
  line or character, the alternate screen buffer (`?1049h`), save and
  restore the cursor
- 256-colour and 24-bit (truecolor) SGR, bold/dim/italic/underline/blink/
  reverse attributes
- Wide-character (CJK) cells and combining marks
- Bracketed paste mode (`?2004h`)
- Mouse reporting (X10/SGR modes), so Mbl and the other TUIs can take
  clicks
- OSC 0/2 window titles, OSC 52 clipboard, OSC 8 hyperlinks
- Cursor shapes (block, bar, underline) and blink, DECSCUSR
- Answers to device status reports (DSR/CPR) and DA queries

## Scrollback and selection
- A scrollback buffer with mouse-wheel and Shift+PgUp/PgDn scrolling
- Mouse text selection and copy, plus paste with middle click or
  Ctrl+Shift+V
- Search in the scrollback (Ctrl+Shift+F)
- Save the scrollback to a file

## Windows and tabs
- Tabs: several sessions in one window, Ctrl+Shift+T / Ctrl+Tab
- Split panes, horizontal and vertical
- Resizing the window resizes the grid and tells the program
  (SIGWINCH-like event)
- Full screen (F11) and a zoom level (Ctrl+= / Ctrl+-)

## Configuration
- A `~/.mbltishi` file with the font, size, colour scheme, scrollback
  length, cursor and bell
- Built-in colour schemes that match Kremlin's wallpapers (Steppe,
  Graphite, ...)
- Choose any installed TrueType font through Petersburg's `loadfont`
- A visual bell, or no bell at all

## Platform reach
- Run on Windows and Linux too, through a pseudo-console (ConPTY,
  forkpty), not only in Moscow
- Start with a working directory and environment: `Mbltishi --cwd DIR
  --env K=V`
- `--hold`: keep the window open after the program exits, showing its
  exit status

## Integration
- A Kremlin launcher argument to open a new tab in a running Mbltishi
- Drag a file from Kremlin or Tver to paste its path
- Clickable `file:line` references that open in Mbl
- Notify Kremlin's message area when a long command finishes in a
  background window

## Tests
- A headless mode that feeds an escape-sequence script and dumps the
  grid, for golden-file tests of the emulator
- vttest-style conformance checks

## Shell integration
- Shell integration marks (OSC 133): jump between prompts, select the
  output of one command, and show its exit status in the margin
- Report the current directory (OSC 7), so a new tab opens in it
- A right-click menu: copy, paste, clear, new tab, settings
- Paste safety: ask before pasting several lines into the shell
- Progress reporting (OSC 9;4) shown on Kremlin's panel button

## Sessions
- Detach and reattach: keep the program running when the window closes
  (`--detach`, `--attach NAME`)
- Named profiles: a program, a directory, colours and a font per
  profile, listed in the new-tab menu
- Record a session with timing (asciinema-style) and replay it
- Broadcast input to all panes
- Log all output to a file (`--log FILE`)

## Graphics and text
- Inline images (Sixel or the Kitty protocol) so Omsk output can be
  shown in the terminal
- Ligatures and bold or italic faces once Pobeda has them
- Line-drawing through the font's box glyphs with pixel-exact joins at
  every zoom
- Background transparency or a dimmed unfocused window
- URL detection without OSC 8: Ctrl+click opens a link
