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
- Environment variable manipulation as first-class commands: `export`,
  `unset`, `env`, `$VAR` expansion in arguments, and per-command
  `VAR=x cmd`
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
