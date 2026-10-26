# USSR optimization cycle

Target: run time, not code size. Each optimization lands in both
implementations: the C runtime (`ussr`) and the self-hosted one
(`src/russia/russia.su`), unless it only makes sense for one of them.
They are ordered from the simplest to the hardest and done in that order.

## Layers

| # | Layer | ussr (C) | russia.su |
|---|-------|----------|-----------|
| 1 | Preprocessor | `pp.c` | `pp.su` (`pp_run`) |
| 2 | Lexer / parser | `lexer.c`, `parser.tab.c` | `lx_scan`, `ps_parse` |
| 3 | AST | command lists (`ussr.h`) | `nd` records |
| 4 | Compiler to UUASM | `ussr_bytecode.c` | `cv_*` |
| 5 | UUASM to VM opcodes | `ussr_uuasm.c` | `ua_to_vm`, `ua_to_bootstrap` |
| 6 | VM dispatch | `main.c` (`vm_run`) | `vm_loop`, `vm_step` |
| 7 | Host services | `ussr.c`, `uno.c` (values, variables, hash) | `rt_read`, `rt_write`, `vm_native*` |

## Benchmarks

`bash bench/run.sh [ussr]` prints the best of three runs of each
`bench/*.su`; the `*_small.su` variants run through russia.su.

## List

| ID | Layer | Optimization | ussr | russia | Gain |
|----|-------|--------------|------|--------|------|
| O0 | build | Compile the C runtime with `/O2` (MSVC) and `-O2` (`ussr-ubuntu.mk`); `OPT=/Od` for debugging | done | done (runs on ussr) | loop 0.472 → 0.107 s, russia loop 2.015 → 0.559 s |
| O1 | 4 | Constant folding: arithmetic, comparison and bitwise operators and unary minus on literal operands become one constant | done | done | loop 0.107 → 0.080 s, russia loop 0.559 → 0.45 s |
| O2 | 7 | Fast paths for the `getf` (struct field) and `at` (vector item) builtins, skipping argument marshalling and the name chains | done | done | fields 0.168 → 0.086 s, russia loop 0.45 → 0.34 s, russia fields 0.51 → 0.45 s (russia side alone) |
| O3 | build | Whole-program optimization: MSVC `/GL` + `/LTCG`, gcc `-flto=auto`, so the VM inlines the value helpers of `ussr.c` / `uno.c` (`ussr_null`, `ussr_value_free`, ...) | done | done (runs on ussr) | call 0.19 → 0.11 s, loop 0.085 → 0.066 s, fields 0.083 → 0.062 s, russia loop 0.33 → 0.24 s, russia fields 0.46 → 0.32 s |
| O4 | 6 | CALL/RET: save and restore only the registers a function can touch (compiler-known high-water mark) instead of all 32 | dropped | n/a | no measurable gain after O3: with the value helpers inlined, moving 32 mostly null registers is cheap |
| O5 | 7 | Fast paths for `setf` (through a silent `ussr_struct_try_set_field`) and `len` on vectors | done | done | setf 0.124 → 0.074 s, russia loop 0.24 → 0.22 s, russia setf 0.317 → 0.30 s (russia side alone) |
| O6 | 4 | Dead branch elimination: `if` / `while` on a constant condition | dropped | dropped | constant conditions are rare (`LOAD_CONST` → `JMP_FALSE` is not among the common pairs) and a constant branch saves no work without removing code |
| O7 | 5 | Peephole: jump-to-jump threading, NOP removal | dropped | dropped | about 250 jumps land on a JMP in a run of millions of instructions |
| O8 | 6 | Integer fast path for `+ - * = != < <= > >=` without the generic `vm_binary` call; russia: jumps and arithmetic read and write registers inline instead of calling `vm_reg` / `vm_setreg` | done | done | elif 0.265 → 0.242 s, loop 0.066 → 0.063 s, russia loop 0.22 → 0.20 s (russia side alone) |
| O9 | 6 | One "slow path" test for the yield and resume checks at the top of the dispatch loop | dropped | n/a | built and measured: no change, the compiler already keeps these checks cheap |
| O10 | 6 | Verify register / constant / string indices once at load and drop the per-instruction checks | dropped | n/a | removing the checks of the hottest opcodes outright gained at most 2%, inside the noise, so a load-time verifier is not worth it |
| O11 | 6 | Superinstructions without a format change: a comparison followed by `JMP_TRUE` / `JMP_FALSE` on its register takes the jump in the same dispatch | dropped | done | ussr: no change (a C dispatch is cheap); russia: elif_small 0.78 → 0.74 s |
| O12 | 4 | Copy propagation: a `LOAD_VAR x` into the register a `STORE_VAR x` just stored from, when nothing jumps there, becomes a NOP | dropped | n/a | no gain once the `LOAD_VAR` leak was fixed (see the note); russia would still dispatch the NOP |
| O13 | 7 | Shared immutable strings: the text of a string value carries a reference count, so copying a value (every register load, store, `getf`, `at`) retains it instead of `strdup` | done | done (runs on ussr) | setf 0.072 → 0.055 s, fields 0.061 → 0.055 s, russia loop 0.205 → 0.191 s, russia elif 0.76 → 0.69 s |
| O14 | 6 | russia: order `vm_step` cases by how often programs run them (loads and stores, jumps, `+`, builtins, `=`, `<`, `-`, CALL, RET first) | n/a | done | russia loop 0.196 → 0.187 s, elif 0.68 → 0.66 s |
| O15 | 1 | Preprocessor: cache included files by path | dropped | dropped | nothing to cache: `$once` already keeps a file from being read twice, and russia.su includes seven distinct files |
| O16 | 7 | String length in the text header (O13), so the `byte`, `slice` and `len` fast paths check bounds in O(1); they used to measure the text up to the index, which made scanning a text quadratic. Replaces the planned russia lexer character table: `lx_alpha_p` / `lx_digit_p` are under 0.1% of a compile | done | done (runs on ussr) | russia compiling Mbl.su 2.9 → 2.4 s, lexbench 0.53 → 0.36 s |
| O17 | 6 | Computed-goto dispatch where the compiler supports it (gcc/clang) | dropped | n/a | O9 to O11 showed dispatch is not the C VM's cost (removing a tenth of the dispatches changed nothing), and MSVC, the main build, has no computed goto |
| O18 | 1-4 | Startup: preprocessing (~10 ms), parsing (~11 ms) and compiling (~16 ms, 6 of them the scope warnings) russia.su itself, before a program runs | dropped | n/a | no single hot spot: the scope warnings' per-instruction local-name scans (quadratic in big functions) were replaced by a mark array with identical warnings and bytecode, and startup did not move (0.075 s) |
| O19 | 7 | End a call by clearing only the locals it assigned (a dirty-slot list) instead of every local slot | dropped | n/a | russia 2-3% faster, native calls ~3% slower: inside the noise either way; D1 re-measured with it, still no gain |
| D1 | 6 | Bind CALL parameters and read the RET value by local slot instead of by name | dropped | n/a | no measurable gain: names are found in the first slots |
| D2 | 7 | O(1) GC registry unlink (doubly linked list) | dropped | n/a | no measurable gain: freed objects sit near the list head |
| D3 | 6 | russia: precomputed backend opcode instead of the `rt_bopt` lookup | dropped | n/a | it is already one vector read per step |

Status values: planned, in progress, done, dropped (with the reason).

## Measuring

The benchmark noise floor is about ±5%: the same machine code moved by
unrelated edits changes timings that much. Compare binaries back to back,
more than once.

`nmake /f Makefile.msc "OPT=/O2 /GL /DUSSR_PROFILE"` builds a profiling
`ussr` that prints cycles and counts per opcode and per called function or
builtin at exit, the most frequent opcode pairs and how many jumps landed
on another JMP. From Git Bash, run nmake through a `.bat` file: MSYS
rewrites a bare `/O2` argument into a path and the build silently drops to
no optimization.

## Notes

### O0

The MSVC build used `/Od`, so every Windows release ran unoptimized.
`nmake /f Makefile.msc OPT=/Od` still builds the debug flavour. All 78
native tests pass with `/O2`. `/O2` exposed a latent bug: `win/getopt.h`
declared `optarg` `dllimport` although `getopt.c` is linked in, and the
optimized `main()` lost the `-e` argument. `STATIC_GETOPT` fixes that.

### O1

`{60 * 60}` used to compile to two `LOAD_CONST` and one `MUL` executed on
every pass; it is now one `LOAD_CONST 3600`. Folding uses the VM's own
operator semantics (in C the VM's `vm_binary`, in russia.su the same
arithmetic the VM step runs), so results are unchanged: `{7 / 2}` is still
`3.5`. It is skipped when the operation would fail at run time (division by
zero, shift count outside 0..63, mixed types), so the error still appears
where it did. `&&` and `||` are not folded: they short-circuit and yield an
operand, not a boolean.

### O2

The profile of russia.su running a loop showed `getf` (~1,100 cycles a
call) and `at` (~850) as the top two costs: russia's VM reads every
instruction field with `getf` and every register with `at`. The general
OOP path allocates an argument array, copies each argument twice and walks
a chain of name comparisons. The VM now handles a struct `getf` and a
vector `at` straight from the argument registers, like the existing
string `byte`/`len`/`slice` fast path; a missing field or an index out of
range still takes the general path and its error message.

russia.su does the same for the programs it runs: `vm_native_kind` gives
`getf` and `at` kinds of their own (9 and 10), which `vm_native` checks
first instead of reaching them through `vm_native_c`'s twenty name tests.

### O3

The VM's hot paths call small functions in other files for every value:
`ussr_null()` and `ussr_value_free()` live in `ussr.c`, so every register
written, every RET restoring 32 registers and every builtin call paid for
out-of-line calls. `/GL` with `/LTCG` (gcc: `-flto=auto`) lets the
compiler inline them across files. `OPT=/Od LTCG=` gives the old debug
build. Function calls got 40% cheaper, which is most of russia.su's gain:
its VM makes about 50 calls per instruction it runs.

### O4 (dropped)

Implemented and measured: the compiler recorded the highest register each
function names, CALL and RET moved only those. No benchmark moved outside
the noise. After O3 the cost of RET is the locals and the result copy,
not the register loop.

### O5

After O3 the profile of russia.su put `setf` (~970 cycles a call) and
`len` (~480) at the top of the builtins. `setf` could not take the
fast path as it was, because `ussr_struct_set_field` prints its errors and
the general path would report them again; `ussr_struct_try_set_field`
does the same work silently and returns 0 for anything the general path
must handle. russia.su gives `setf` and `len` their own kinds (11, 12).

### O6 and O7 (dropped)

The profiler's opcode pairs decided both before any code was written. In
russia.su running a loop (about 10 million instructions) only ~250 jumps
landed on a JMP, and a constant `if` condition (`LOAD_CONST` then
`JMP_FALSE`) is not among the common pairs. The common ones are
`LOAD_VAR LOAD_CONST EQ JMP_FALSE` (russia's `elif(_): {op = N}` chains,
~665,000 times) and `STORE_VAR` followed by `LOAD_VAR` (~1,080,000): the
targets of O10 and O11.

### O8

Each instruction costs about 30 cycles, so the call into `vm_binary` and
its type dispatch were a visible share of an `ADD` or `EQ`. Two integers
now take an inline switch using the same `ussr_int_*` helpers. russia.su
has no such dispatch of its own (its VM uses the host's operators), so its
part of O8 is the matching overhead there: `vm_step`'s jumps, arithmetic
and comparisons read and write `vm_regs` inline instead of through
`vm_reg` / `vm_setreg`, three user function calls per instruction.

### O9 to O11: dispatch overhead

Three attempts at the C VM's per-instruction overhead, all built and
measured, none outside the noise: merging the yield/resume tests (O9),
deleting the bounds checks of `LOAD_CONST`, `LOAD_VAR`, `STORE_VAR` and the
jumps (the best case for O10), and fusing compare-and-branch (O11). A
C-level dispatch is cheap; the ~30 cycles of a `LOAD_VAR` or `LOAD_CONST`
are the variable lookup and the value copy (a `strdup` for every string),
which is what O12 and O13 address.

russia.su is the opposite: one of its dispatches is a whole `vm_loop` pass
with several user function calls, so the fused compare-and-branch
(`vm_fuse`, after the `= != < <= > >=` steps) saves real work there.
Opcodes are unchanged; UUASM, `.ru` files and the C VM need nothing.

### O12 (dropped) and the LOAD_VAR leak

The pass was built (`STORE_VAR x` then `LOAD_VAR x` into the same
register, not a jump target, function entry or CALL return point) and was
*slower*. The reason was a bug: `LOAD_VAR` overwrote its register without
freeing the old value, so every reload of a string leaked a copy and every
reload of a vector or struct leaked a reference. A NOP kept the old value,
which the next write then had to free. Printing a string variable four
million times peaked at 186.7 MB; with `LOAD_VAR` freeing the old value it
stays at 4.4 MB. The fix costs about 3% on the benchmarks and is kept;
against the fixed VM the copy propagation gained nothing.

### O13

`data.string` stays a plain `char *`, so the hundreds of places that read
string values are unchanged. What changed is who owns the bytes: the text
is allocated by `ussr_text_alloc` / `ussr_text_new` / `ussr_text_dup` with
a reference count in front of it, `ussr_value_copy` (and uno's
`uno_store_value`) retains it, and `ussr_value_free` (and uno's release
and destroy paths) releases it. Only five places build a string value
themselves (`ussr_string`, the parser's string literal, the UNO decoder,
the concat builtin, uno's store); all now go through these functions.
String values must never be modified in place or passed to `free()`.

Checked with the normal suites (ussr 78/78, russia 75/77 as before) and an
AddressSanitizer build (`"OPT=/Od /fsanitize=address" LTCG=`): all 78
native tests and a sample of 13 programs through russia.su ran with no
reports.

### O16

Profiling russia.su compiling Mbl.su (2.9 s) put the `byte` builtin at
~9,400 cycles a call and `slice` at ~19,600: their fast paths found the
bounds with `vm_text_reach(text, index + 1)`, a scan from the start of
the text, so a lexer walking a 45 KB file byte by byte did quadratic work.
The text header from O13 now also keeps the length (computed once, when
the text is created or first measured; text is never modified), and
`byte`, `slice` and `len` use `ussr_text_length`. The compiled `.ru` file
is byte for byte the same.

The planned russia lexer character table (the old O16) is not worth
doing: the same profile has `lx_alpha_p` and `lx_digit_p` under 0.1%.

### Where the cycle stands

Every item on the list has been tried, most of them built and measured.
Kept: O0, O1, O2, O3, O5, O8, O11 (russia), O13, O14, O16, plus the
`LOAD_VAR` leak fix. Against the `/Od` build the cycle started from:

| Benchmark | Before | After |
|-----------|--------|-------|
| `loop.su` | 0.457 s | 0.066 s |
| `call.su` | 1.337 s | 0.113 s |
| `fields.su` | 0.422 s | 0.055 s |
| `setf.su` | | 0.055 s |
| `loop_small.su` through russia.su | 2.015 s | 0.187 s |
| russia.su compiling Mbl.su | | 2.35 s (2.9 s before O16) |

What is left in a profile is the work itself: variable lookup and value
copies in `LOAD_VAR` / `LOAD_CONST` / `STORE_VAR`, and the locals and
result copy of RET. The next real gains need structural changes:
resolving global variables to slots at compile time (instead of by name
in a hash), and in russia.su a VM whose instructions are not structs read
field by field with `getf`.
