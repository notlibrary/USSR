# Changelog

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
