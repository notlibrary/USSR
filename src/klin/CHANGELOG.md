# Changelog

## 0.2.0
Refine pass 1 from REFINE.md; the new code is `more.su`.
- Keys: memory (`MC MR M+ M- MS`), `ANS` and a tape of results, `RAND`,
  `GRAD` (and `RAD`) angles, `SINH COSH TANH ASINH ACOSH ATANH CBRT
  GAMMA`, `N!` of non-integers through the gamma function, `DMS`/`DD`,
  and the binary `NPR NCR GCD LCM MOD YROOT LOGY` in casual and sci.
- Programmer mode: word sizes `BYTE WORD DWORD QWORD`, `SIGNED`/
  `UNSIGNED`, `NAND NOR XNOR`, `SAR` beside a logical `SHR`, `POPCNT CLZ
  CTZ`. AC keeps memory, the tape and these settings.
- Display formats `FIX SCI ENG AUTO`.
- Library: `klin_calc` evaluates typed expressions (implicit
  multiplication, `200 + 10%`, `!`, functions, constants c g h na k,
  variables and user functions `f(x) = ...`), `klin_solve`,
  `klin_derivative`, `klin_integral`, `klin_rpn`, `klin_convert` (length,
  mass, temperature, data, time), dates (`klin_days_between`,
  `klin_add_days`, `klin_weekday`), `klin_stats`, `klin_regression`,
  `klin_ieee`, `klin_bytes`, `klin_char`, `klin_code`, `klin_checksum`,
  `klin_group`, the tape and settings files.
- `ussr src/klin/klin.su -e "sin(30)+2^10" ...` prints results.
- `tests/klin_more_test.su`: 76 checks; `tests/klin_test.su` and the GUI
  test are unchanged.

## 0.1.0
- First version: casual, scientific and programmer modes in one window,
  seven-segment display, keyboard input.
- Scientific: trigonometry in degrees or radians, logarithms, powers,
  roots, factorial, constants, parentheses and operator precedence.
- Programmer: 64-bit integers in hex, decimal, octal and binary, bitwise
  operators, shifts, rotations and SW16/SW32/SW64 byte swaps for
  endianness conversion.
- Added `tests/klin_test.su` (engine) and `tests/gui/klin_test.su` (GUI).
