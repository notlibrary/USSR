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
| O2 | 6 | russia: dispatch on a precomputed backend opcode instead of looking it up in `rt_bopt` on every step | n/a | planned | |
| O3 | 6 | russia: order `vm_step` cases by frequency (registers, arithmetic, jumps first) / split the chain into ranges | n/a | planned | |
| O4 | 4 | Dead branch elimination: `if` / `while` on a constant condition | planned | planned | |
| O5 | 5 | Peephole: jump-to-jump threading, NOP removal | planned | planned | |
| O6 | 6 | Integer fast path for `+ - * < <= > >= =` without the generic `vm_binary` call | planned | planned | |
| O7 | 6 | Verify register / constant / string indices once at load and drop the per-instruction checks | planned | n/a | |
| O8 | 6 | One "slow path" flag for quantum, yield and resume checks at the top of the dispatch loop | planned | planned | |
| O9 | 7 | Cache global variable lookups (resolve names to slots at compile time) | planned | planned | |
| O10 | 6 | Superinstructions: compare-and-branch, add-immediate | planned | planned | |
| O11 | 4 | Copy propagation: compute straight into the target instead of a temp register plus `STORE_VAR` | planned | planned | |
| O12 | 1 | Preprocessor: cache included files by path | planned | planned | |
| O13 | 2 | russia lexer: character class table instead of comparison chains | n/a | planned | |
| O14 | 7 | Shared immutable strings (refcount) instead of copying on every register load | planned | planned | |
| O15 | 6 | Computed-goto dispatch where the compiler supports it (gcc/clang) | planned | n/a | |

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
