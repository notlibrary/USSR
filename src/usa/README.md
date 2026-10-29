# usa

A small C compiler written in USSR. It compiles C99 to one `.ru` file,
the USSR VM's bytecode ([RU_BYTECODE_FORMAT.md](../../docs/RU_BYTECODE_FORMAT.md)),
which `ussr` runs like any other program. There is no REPL and no
interpreter: C in, `.ru` out.

```bash
ussr src/usa/usa.su hello.c                  # writes hello.ru
ussr hello.ru
ussr src/usa/usa.su -o lemon.ru lemon.c      # several files link into one
ussr src/usa/usa.su -DPIKCHR_SHELL -I inc pikchr.c
ussr src/usa/usa.su -E file.c                # preprocess only
```

Options: `-o out.ru`, `-I dir`, `-D NAME[=VALUE]`, `-E`, `-nostdlib`,
`-trace` (each function says its name on stderr as it is entered: the
last line before a VM error names the culprit). `-c`, `-O*`, `-g`, `-W*`,
`-f*`, `-m*`, `-std=`, `-l*`, `-L*` are accepted and ignored, so makefile
lines work. usa finds its `include/` and `libc/` through `USA_HOME`, else
in `src/usa`, `usa` or `.` from the current directory.

## Pieces

| File | What |
|------|------|
| `usa.su` | driver, linker, `.ru` writer |
| `cpp.su` | the preprocessor: russia's `pp.su` with `#` for `$`, plus `#if` expressions, `#ifndef`/`#undef`/`#error`/`#warning`/`#pragma once`, `#` and `##`, `...`/`__VA_ARGS__`, `<...>` includes, `__FILE__`/`__LINE__`, comments and backslash-newlines, calls spread over lines |
| `lex.su` | tokens |
| `parse.su`, `expr.su` | hand-written recursive-descent parser: types, scopes, declarations, initializers, expressions with C's conversions, statements |
| `gen.su` | code generator for the USSR VM |
| `include/` | the C library headers |
| `libc/libc.c` | the C library, in C, compiled with every program |

## The C machine on the USSR VM

- **Memory** is one USSR vector, `M`; an address is an index into it.
  Objects have their real C sizes and alignments (LP64: `long` and
  pointers are 8 bytes) and each scalar lives in the cell at its address,
  so `sizeof`, pointer arithmetic, `memcpy` and `memset` behave. Static
  data starts at 8, then the stack (1M cells), then the heap, which
  `malloc` grows by pushing cells.
- **Locals** whose address is never taken are VM locals; arrays, structs
  and the rest get a frame on the stack.
- **Calls** are VM calls with the arguments in registers. Variadic
  arguments go to the stack as 8-byte cells; a struct goes by address and
  comes back through a per-function buffer. A pointer to a function is
  its index + 1, and a call through it goes to a generated dispatcher.
- **Integers** are VM integers kept in their C type's range: narrowing
  masks or sign-extends, `unsigned int` arithmetic wraps at 32 bits,
  `unsigned long` compares, divides and shifts as unsigned. Plain `char`
  is unsigned.
- **Reals**: `double` and `float` are VM reals (`float` keeps double
  precision; `sizeof(float)` is 4).
- **Bytes of objects**: since a scalar is one cell, `fwrite`, `fread`,
  `memcpy` and `memmove` between a typed object and bytes are compiled to
  `__fwrite_t`/`__fread_t`/`__memcpy_t` with the object's layout, which
  make and read the real little-endian (IEEE-754) bytes.

## The C library

stdio (files are read whole when opened and written whole when flushed;
stdout is line buffered), printf/scanf families (doubles are printed
exactly, rounding half to even, as glibc), stdlib (malloc with size
classes, qsort, bsearch, strtod, getenv, atexit), string, ctype, math
(sin and ln are USSR's; the rest is computed), time (local time through
USSR's `localtime`), stdarg, setjmp (stub), getopt/getopt_long, pwd,
unistd bits, an identity libintl, assert, errno, limits, float, stdint,
inttypes, stdbool, stddef, signal (stub), locale (C only).

## Battle-tested on

Real projects compiled by usa and compared with gcc builds:

- **pikchr** (8.3K lines, the diagram language): all 102 of its test
  diagrams render to the same bytes as the gcc build.
- **lemon** (6.1K lines, SQLite's LALR(1) parser generator): run on
  `pikchr.y` it writes `pikchr.c`, `pikchr.h` and the `pikchr.out` report
  identical to gcc-built lemon's.
- **tpm** (the Toothpaste Picking Manager, 4K lines in three files): its
  own test battery passes (with a small stand-in for libcheck).
- **prng_xrp** (the XRP generators): test programs 1-7 print gcc's
  numbers.
- **LINPACK** (emscripten's `linpack2.c`, order 100): gcc's results.

## Limits

- No `long double` beyond `double`, no complex numbers, no `_Atomic`,
  no threads, no inline assembly (skipped), no K&R definitions with
  separate parameter declarations.
- `setjmp` always returns 0 and `longjmp` ends the program: the VM's call
  frames cannot be unwound.
- Reading a scalar through a pointer of another type (`*(uint32_t *)buf`)
  sees the cell, not the bytes; go through `memcpy` for type punning.
- A division by zero, integer or real, stops the program (the VM's rule).
- The VM limits a program to 1M instructions, 1,024 nested calls and
  2 billion executed instructions.
- `system()` returns -1; there are no signals.
