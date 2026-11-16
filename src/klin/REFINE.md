# Klin — refinement ideas

**Today:** a desktop calculator on a Petersburg canvas with three modes.
**Casual** has the four operations, percent, sign and backspace. **Sci**
has trigonometry in degrees or radians, ln/log, powers, roots, factorial,
PI/E, parentheses and precedence climbing. **Prog** works on 64-bit
integers in HEX/DEC/OCT/BIN with bitwise operators, shifts, rotations
and SW16/32/64 byte swaps. It draws a seven-segment display and keeps a
12-digit format. The engine runs without a window.

**Refine pass 1 (2026-11-16):** [x] done, [~] partly done; see
CHANGELOG.md 0.2.0. The new code is more.su.

## Calculation
- [~] Memory keys: `MC MR M+ M- MS` and several memory slots — partly: MC MR M+ M- MS with one memory slot
- [~] `ANS` (the last result) and a history tape of past results, any of
  which can be reused with a click — partly: ANS and a tape of "expr = result" lines (klin_tape, klin_tape_save); no clickable history pane yet
- [x] Hyperbolic functions `SINH COSH TANH` and their inverses
- [x] Gradians as a third angle unit
- [x] `nPr`, `nCr`, `GCD`, `LCM`, `MOD` in sci mode, plus `x^(1/y)`,
  `log_y(x)`, `cbrt`
- [x] Gamma for non-integer factorial
- Arbitrary precision or decimal arithmetic for money (0.1 + 0.2 = 0.3)
- Fractions mode (`3/4 + 1/6 = 11/12`) and mixed numbers
- Complex numbers (`i`, polar and rectangular forms)
- [~] Engineering notation (exponents in multiples of 3) and fixed or
  scientific display settings — partly: ENG, SCI, FIX and AUTO display formats
- [~] Random numbers (`RAND`, `RANDINT`) — partly: RAND; no RANDINT

## Programmer mode
- [x] Word sizes: 8, 16, 32 and 64 bits, signed or unsigned
- A bit grid that toggles single bits with clicks
- [~] `NAND NOR XNOR`, arithmetic versus logical right shift, `POPCNT CLZ
  CTZ` — partly: NAND NOR XNOR, SAR (arithmetic) beside a now logical SHR, POPCNT CLZ CTZ
- [~] IEEE 754 view: the float/double bits of a value, editable — partly: klin_ieee shows sign, exponent and the bits of a double; not editable
- [~] ASCII/Unicode of the low byte(s), and a character-to-code input — partly: klin_char and klin_code; no key yet
- [~] Endianness view of the bytes in memory order, as well as swaps — partly: klin_bytes gives the memory order; no view in the window
- [~] Checksums: CRC-32, Adler, FNV-1a of a typed string — partly: klin_checksum (crc32, adler32, fnv1a)

## More modes
- [x] Unit conversion: length, mass, temperature, data sizes (KiB vs KB),
  time
- [x] Date calculation: days between dates, add days, weekday
- [~] A statistics mode: data entry, mean, median, standard deviation,
  linear regression — partly: klin_stats and klin_regression as library calls; no data-entry mode
- [~] RPN mode with a visible stack (HP-style) — partly: klin_rpn evaluates RPN text; no stack display
- A small graphing mode: plot `y = f(x)` on the canvas
- A currency converter with rates loaded over Ryazan HTTPS (opt-in)

## Interface
- Paste an expression from the clipboard and evaluate it, copy a result
- [~] Type a whole expression at the keyboard (`2*(3+4)` Enter) — partly: klin_calc evaluates typed text; the window does not take it yet
- [~] Remember the window size, mode, angle unit and base across runs
  (`~/.klin`) — partly: klin_settings_save and _load (mode, angle, base, word, format); the window does not call them yet
- Themes and a high-contrast scheme; scale for HiDPI as Kremlin does
- Hover tooltips naming each key's function
- An accessible text display mode (no seven-segment)

## Integration
- [x] A command-line form: `ussr klin.su -e "sin(30)+2^10"` prints the
  result, for scripts
- [~] Expose the engine as a library to the USSR REPL (`calc(_): "..."`) — partly: klin_calc and the other klin_* calls are the library
- A Kremlin panel mini-calculator

## Tests
- Property tests: compare results with the VM's own `math.su` across
  random inputs
- Golden screenshots of each mode for the GUI test

## Expression and entry
- [x] Implicit multiplication (`2PI`, `3(4+5)`) and percent of the previous
  operand (`200 + 10%` = 220), stated in the docs
- [x] Constants library: c, g, h, Avogadro, Boltzmann, with units shown
- [x] User variables (`x = 5`) and user functions (`f(x) = x^2 + 1`)
- [x] A solver: roots of `f(x) = 0`, numeric derivative and integral
- Number bases with fractions in prog mode (`0.1` in binary)
- Bit-width overflow flags (carry, overflow, zero, sign) after each
  prog operation
- Shift counts taken modulo the word size or flagged, as an option
  (counts are limited to 0..63 today)
- Signed and unsigned division and comparison keys
- [~] Sexagesimal entry and output (degrees-minutes-seconds, hours) — partly: DMS and DD keys convert degrees to and from d.mmss
- [~] A paper-tape mode: every entry and result on a scrolling roll that
  can be saved or printed — partly: the tape is kept and can be saved; no scrolling roll in the window
- Tax and tip keys, and a money mode fixed to two decimals

## Display
- [~] More than 12 significant digits on request, with digit grouping — partly: klin_group adds digit grouping; the display keeps 12 digits
- An expression editor with a cursor: fix a mistake in the middle
  without retyping (today Backspace deletes a digit)
- Results as exact forms where possible (`sqrt(8)` = `2*sqrt(2)`,
  multiples of PI)
- A resizable window with a larger history pane
- Speech output of the result (accessibility)
