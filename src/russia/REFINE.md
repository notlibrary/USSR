# russia — refinement ideas

**Today:** the USSR interpreter written in USSR (`russia.su`, ~9 300
lines) with:
- `pp.su`: a port of the preprocessor.
- `check.su`: the compile-time diagnostics of the lexer, parser and
  bytecode compiler, with ussr's messages.
- `uuasm.su`: a textual UUASM assembler, disassembler and executor.
- Stages: a lexer, a parser (with alias rewriting), a tree-walking
  runtime, constant folding (O1), a register compiler, a VM, and a
  scheduler for async tasks.
- A REPL with raw-terminal line editing, history and PS1/PS2 prompts.
- `eval`, structs through the host, and aliases.
- `cheat_bootstrap.su` (the nested-eval route) and `lexbench.su`.

## Shell features (from AGENTS.md)
- History and AI-based autocomplete in the REPL: Tab completion of
  builtins, definitions, file paths and variables, plus an opt-in model
  suggestion through `prompt(_):` (Lenin's provider plumbing)
- Reverse incremental history search (Ctrl+R) and `!!` / `!n` history
  expansion
- Keyboard (re)bindings: a `bind(_): "ctrl-a" "line-start"` table loaded
  from `kiev.su`, emacs and vi editing modes
- Environment variable manipulation beyond today's `getenv`, `setenv`
  and `unsetenv`: an `env` listing, `$VAR` expansion in arguments,
  per-command `VAR=x cmd`, and export to child programs made explicit
- A built-in line-based editor in the REPL: multi-line editing of the
  current block, `\e` to edit the last entry in Mbl, recall of whole
  definitions
- A comfortable config file: one `kiev.su` with sections (prompt,
  aliases, bindings, colours), `reload(_):`, and errors with
  line numbers
- Command chaining and named pipes: `a | b`, `a && b`, `a || b`, `>`,
  `>>`, `<`, `2>&1`, and FIFOs (`mkfifo`) for process redirection
- Boolean expression-based execution: `if {cmd}` on exit status,
  `test`-like predicates (`-f`, `-d`, string and number comparison)
- Bytecode VM for command execution: run the REPL line through the
  compiler and VM path, not the tree walker, and cache compiled
  definitions
- async/await for I/O: `await` on sockets and files in REPL lines, plus
  a `jobs` / `fg` / `bg` view of scheduler tasks

## REPL
- Syntax highlighting while typing (the token stream exists)
- Bracket matching, and auto-indent on continuation lines
- Inline errors: highlight the failing token from `ck_run`'s location
- `:help NAME` showing a builtin's contract (from the header comments)
- `:time`, `:trace`, `:dis` (show the bytecode or UUASM of an entry)
- A persistent variable inspector (`:vars`, `:defs`)
- Session save/restore (`:save session.su`)

## Interpreter parity and speed
- Close the gaps with the C `ussr`: every builtin the host has, with the
  same errors (a parity test listing them)
- Direct-threaded dispatch, or superinstructions in the VM loop
- Inline caches for globals and calls (the store already has
  last-slot hints)
- Constant folding O2: propagation, dead branch removal, loop-invariant
  hoisting (docs/OPTIMIZATIONS.md)
- `.ru` bytecode output from russia's compiler (a self-hosted compile
  path) and loading `.ru` files
- A profiler: instruction counts per function and hot spots
- A debugger: breakpoints, step, a stack view and watches on the VM

## Self-hosting
- Reach the M5/self-host milestones (docs/BOOTSTRAP_M5.md,
  BOOTSTRAP_SELFHOST.md): russia running russia running a test
- Remove `cheat_bootstrap.su` once real self-hosting passes
- Run `tests/run_all.sh` under russia-on-russia in CI (slow-lane job)

## Diagnostics
- Runtime errors with a call stack (function names and lines)
- Suggestions for unknown names ("did you mean ...?") using edit
  distance
- Warnings: unused variables, shadowed globals, unreachable code

## UUASM
- Support EXTERNAL, EVAL, SCAN, CHAIN, FILE and TEMPLATE in uuasm.su
  through russia's own runtime
- A UUASM pretty-printer with labels and comments
- An optimiser pass over UUASM

## Tests
- Differential tests: the same program under ussr and russia, comparing
  output and exit status for every file in `tests/`
- Fuzz the lexer and parser against `check.su` and the C parser
- Lexer and VM benchmarks tracked per release (lexbench.su exists)

## Language growth (tried in russia first)
- Named arguments and default values for definitions
- Modules or namespaces, so libraries need no name prefixes
  (`Mbl_`, `km_`, `nv_`, `sb_` are conventions today)
- Local variables by declaration, with a warning on accidental globals
- Closures or function values, and `map`/`filter` over vectors
- Pattern matching on vectors, structs and UNO literals
- String interpolation (`"total: {n}"`)
- Exceptions with values (`try` exists): `throw` with data, `finally`
- Iterators and a `for(x): vector [ ... ]` loop
- Integer division and modulo operators that stay integers ("/" always
  yields a real today)
- Byte strings that can hold NUL, ending the "vectors of 0..255"
  workaround in Dubna, Omsk and Novgorod
- Vectors with insert and delete in the middle, and slices

## Embedding and tools
- russia as a library: `ru_eval(v): text` for other USSR programs
  (Lenin sandboxes, Novgorod's "run this" snippets)
- Resource limits per evaluation: steps, memory, time, no file or
  socket access (a sandbox mode)
- A formatter (`russia.su --fmt file.su`) and a linter built on
  `check.su`
- A documentation extractor from header comments
- A language server built from the lexer, parser and `check.su`
- A tracing mode that prints each command with its arguments and result
- Coverage: which lines of a `.su` file a test run executed
- Time-travel or reverse step in the VM debugger for small programs

## Cleanliness
- Split `russia.su` (9 300 lines) into files by stage (`lx`, `ps`,
  `rt`, `cv`, `vm`, `sc`, `repl`) with `$include`
- Replace the stage notes at the top ("Stage M1: lexer + token dump")
  with the current state
- A written list of deliberate differences from the C interpreter,
  if any remain
