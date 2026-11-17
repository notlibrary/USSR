# Changelog

## 0.2.0
- Refine pass 1:
  - `_Generic` selection, with pointer types matched through their
    targets.
  - `typeof`, `__typeof__` and `__typeof` of a type or of an expression
    (not evaluated).
  - `__COUNTER__`.
  - GCC's bit builtins: `__builtin_popcount`, `clz`, `ctz`, `ffs` and
    `parity` (with their `l` and `ll` forms), and `__builtin_bswap16`,
    `32` and `64`.
  - `__builtin_offsetof`, `__builtin_constant_p` (0),
    `__builtin_assume_aligned`, `__builtin_prefetch`, `__builtin_memcpy`
    and `__builtin_memset`.
  - `-w` drops warnings; `-Werror` turns the first warning into an error
    and writes no `.ru`.
  - `tests/refine1.c` with its expected output `tests/refine1.txt` covers
    these, plus case ranges, `?:` without a middle, VLAs, flexible array
    members, `_Static_assert` and `_Alignof`.
- Floating constants are converted exactly at compile time (round to
  nearest even, subnormals, hex floats); `DBL_MAX` no longer became
  infinity, which made printf of an infinity loop forever.
- `va_arg` of a struct or union reads the object its cell points to.
- `fmod` is exact; `strtod` reads hex floats; `fwrite` to a read-only
  stream writes nothing; scanf takes `%F` and `%A`.
- `f`/`l` variants and `__builtin_` names of the math functions,
  `copysign`, `scalbn`, `exp2`, `toascii`, `rand_r`, `strtold`,
  `asprintf`, `vasprintf`, `DBL_TRUE_MIN`, more `PRI*` macros, the
  `<stdint.h>` types in `<sys/types.h>`, `__builtin_expect`/`trap`.
- Checked against emscripten's `test/core` corpus (see README).

## 0.1.0
- First version: C99 to `.ru` bytecode for the USSR VM, written in USSR.
  Preprocessor forked from russia's `pp.su` (`#` directives, `#if`
  expressions, `#`/`##`, variadic macros), hand-written lexer and
  recursive-descent parser, code generator, linker and `.ru` writer.
- C memory as one USSR vector with real sizes and alignments; VM locals
  for scalars, a stack frame for the rest; variadic calls, structs by
  value, function pointers through dispatchers, variable length arrays.
- A C library in C: stdio, printf/scanf, stdlib, string, ctype, math,
  time, getopt, pwd, libintl stand-in; exact double formatting;
  little-endian bytes for `fwrite`/`fread`/`memcpy` of typed objects.
- Builds pikchr, lemon, tpm, prng_xrp and LINPACK with gcc's output.
