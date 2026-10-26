# Klin test plan

Automated:

- `ussr tests/klin_test.su` (any build, also run through russia.su by
  `tests/run_all.sh`): 92 key sequences across the three modes: precedence,
  parentheses, repeated and replaced operators, chaining after `=`, the
  12-digit and exponent formats, every scientific function in degrees and
  radians with its domain errors, integer division and `MOD` signs, every
  base, two's complement, shifts and rotations, SW16/SW32/SW64, entry
  limits, and values kept across mode switches.
- `ussr tests/gui/klin_test.su` (GLFW build and a display): clicks keys on
  the canvas, types on the keyboard (Enter, Escape, Backspace, Shift, keypad,
  hex letters), switches modes with the toolbar and checks the pressed
  button, the DEG/RAD key, base keys and a dimmed digit key.

Manual:

- Run `ussr src/klin/klin.su` and each mode; check the keypad fills the
  window, labels are centered, and long results shrink to fit the display.
- In PROG, enter a negative number and check the HEX/OCT/BIN lines show the
  64-bit two's complement and BIN wraps onto a second row.
