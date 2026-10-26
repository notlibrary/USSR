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
| O3 | 6 | CALL/RET: save and restore only the registers a function can touch (compiler-known high-water mark) instead of all 32 | planned | planned | |
| O4 | 7 | Fast path for `setf` and `len` on vectors (`setf` needs a silent field/type check first) | planned | planned | |
| O5 | 4 | Dead branch elimination: `if` / `while` on a constant condition | planned | planned | |
| O6 | 5 | Peephole: jump-to-jump threading, NOP removal | planned | planned | |
| O7 | 6 | Integer fast path for `+ - * < <= > >= =` without the generic `vm_binary` call | planned | planned | |
| O8 | 6 | One "slow path" flag for quantum, yield and resume checks at the top of the dispatch loop | planned | planned | |
| O9 | 6 | Verify register / constant / string indices once at load and drop the per-instruction checks | planned | n/a | |
| O10 | 6 | Superinstructions: compare-and-branch, add-immediate | planned | planned | |
| O11 | 4 | Copy propagation: compute straight into the target instead of a temp register plus `STORE_VAR` | planned | planned | |
| O12 | 7 | Shared immutable strings (refcount) instead of `strdup` on every register load | planned | planned | |
| O13 | 6 | russia: order `vm_step` cases by frequency / split the chain into ranges | n/a | planned | |
| O14 | 1 | Preprocessor: cache included files by path | planned | planned | |
| O15 | 2 | russia lexer: character class table instead of comparison chains | n/a | planned | |
| O16 | 6 | Computed-goto dispatch where the compiler supports it (gcc/clang) | planned | n/a | |
| D1 | 6 | Bind CALL parameters and read the RET value by local slot instead of by name | dropped | n/a | no measurable gain: names are found in the first slots |
| D2 | 7 | O(1) GC registry unlink (doubly linked list) | dropped | n/a | no measurable gain: freed objects sit near the list head |
| D3 | 6 | russia: precomputed backend opcode instead of the `rt_bopt` lookup | dropped | n/a | it is already one vector read per step |

Status values: planned, in progress, done, dropped (with the reason).

## Notes

### O0

The MSVC build used `/Od`, so every Windows release ran unoptimized.
`nmake /f Makefile.msc OPT=/Od` still builds the debug flavour. All 78
native tests pass with `/O2`.

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
