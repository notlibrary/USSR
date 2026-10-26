# Klin

Klin is a small desktop calculator written in USSR on a Petersburg canvas,
with three modes in one compact window. Start it from the repository root
with a GLFW-enabled build:

    ussr src/klin/klin.su              # casual mode
    ussr src/klin/klin.su sci          # scientific
    ussr src/klin/klin.su prog         # programmer

The toolbar switches between **CASUAL**, **SCI** and **PROG**; the value on
the display carries over. Click the keys or type:
digits, `.`, `+ - * /` (also on the keypad), `Shift+9`/`Shift+0` for
parentheses, `Shift+6` for a power, `Enter` or `=` for the result,
`Backspace` to delete a digit and `Esc` to clear. In programmer mode `A`-`F`
type hex digits.

**Casual**: the four operations, percent, sign change and backspace.

**Sci**: `SIN COS TAN` and their inverses in degrees or radians (the `DEG`
key toggles), `LN LOG` (base 10), `e^x 10^x x^2 x^y`, `SQRT 1/X N! |X|`, the
constants `PI` and `E`, and parentheses. Operators follow the usual
precedence and `x^y` binds right to left (`2^3^2` is 512).

**Prog**: 64-bit integers. `HEX DEC OCT BIN` switch the base of the main
display; the panel always shows the value in all four (two's complement
for negatives) with the bits grouped by four. Operators `+ - * /` (integer
division toward zero), `MOD`, `AND OR XOR NOT`, `SHL SHR` (shift counts
0..63) and the rotations `ROL ROR`. Endianness: `SW16`, `SW32` and `SW64`
reverse the byte order of the low 2, 4 or 8 bytes (`0x12345678 SW32` is
`0x78563412`). Digit keys that are not digits in the current base are
dimmed.

Functions apply to the number on the display at once, like a desk
calculator; `+ - * / x^y` and the bitwise operators build an expression that
`=` evaluates. Results show up to 12 significant digits, switching to
`mantissa E exponent` outside 1e-7 to 1e12. Division by zero, square roots
of negative numbers and other undefined results show `Error` with the reason
on the expression line; the next key clears it.

## Implementation

All of Klin is USSR code in `klin.su`. The engine (typing, the expression,
evaluation by precedence climbing, number formatting, base conversion, the
byte swaps) works without a window and is what `tests/klin_test.su` checks;
the GUI draws a seven-segment display and the keypad on one canvas and
turns clicks and keys into the same key names. See `docs/API.md`.
