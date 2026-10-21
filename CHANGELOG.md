# USSR programming language CHANGELOG

## [Unreleased]

### Added
- `ussr -c script.su [out.ru]` (`--compile`) compiles to a `.ru` bytecode file without running it; `ussr program.ru` validates and runs it (signature, format and VM ABI versions, CRC-32, bounds). Blocks passed to commands (`@` chaining) are stored as syntax (format 3) and run as from source; `tests/ru/blocks.su` round-trips one.
- Built-in `Vec2`/`Vec3` struct types with `+ -`, scaling by `* /` and `= !=` in expressions; `src/libs/vectors.su` adds `vdot`, `vcross`, `vlen`, `vnorm`, `vlerp` and more.
- `getenv`, `setenv` and `unsetenv` builtins (UTF-8 on Windows).
- REPL prompts from `PS1` and `PS2` with `\u \h \w \W \v \$ \\` escapes; piped stdin works on every platform.
- Builtins for apps written in USSR: `list_dir`, `is_dir`, `getcwd`, `make_dir`, `rename_path`, `remove_file`, `remove_dir`, `read_line`, `typeof`, `fail`, `chr`.
- Preprocessor `$once` include guard and `__MAIN__` for files that are both a library and a program.
- `src/libs/strings.su` (number/text conversion, UTF-8 helpers) and `src/libs/vectors.su`.
- Mbl: UTF-8 (Russian) editing by character, count prefixes, `w`, `cw`, and multi-level undo `u` / redo `Ctrl+R`.
- Tver: directory selection with Insert, held-arrow repeat, direct launch with `ussr src/tver/tver.su`.
- Tula: DXF read/write, zoom, adaptive grid, toolbar, direct launch with `ussr src/tula/tula.su`.
- Petersburg: canvas and toolbar widgets, `keyup`/`focus`/`wheel` events, event injection for tests, timers while the GUI runs.
- russia.su assembles and runs textual UUASM (`.ua`).
- Continuous integration (`.github/workflows/ci.yml`): Ubuntu with GLFW under Xvfb, Ubuntu without GLFW, and Windows (MSVC, GLFW 3.5.1) build and run every test on pull requests and pushes to `main`.
- Function-local variables: every call has its own parameters, return variable and every variable the function assigns, so helpers and recursion no longer overwrite each other's temporaries (before, `fib(a): m1` / `fib(b): m2` style recursion computed `fib(20)` as 1). Names a function only reads are globals. `global(_): names` declares shared state, at the top level for every function or inside one function; `global(_): "*"` restores the old behavior. `eval` inside a function uses its locals; processes started in a function see them as globals. The compiler warns (`file:line: warning:`) about reading a local before setting it, setting a local only other code reads, and reading a global that is only ever set as a local. Variable accesses inside functions are resolved at compile time. See "Variables in functions" in the manual; tests in `tests/locals.su`, `tests/locals_legacy.su`.
- Compile errors say where and why: `file:line: reason`, with the original file and line also inside `$include`d files (the preprocessor keeps a line map). Every compiler failure path reports its command; syntax errors and bad characters use the same format; the REPL reports `<stdin>:N`, `eval` text `<eval>:N`. `tests/errors/run_error_tests.sh` checks 22 bad programs.
- `tests/run_all.sh` runs every suite, checks each standalone test script against `tests/expected/` (stdout, stderr, exit status) and runs the GUI tests; `--update` rewrites the expectations.

### Changed
- Integers are 64-bit on every platform (they were C `long`, 32-bit on Windows: a literal like 3000000000000 was cut to 32 bits, so `tests/math/test_log_hypot.su` printed 3.037e+09 instead of 5e+12, and overflow hung `str_from_number`). `+ - *` wrap around instead of being undefined, `x % -1` no longer traps, integer `=`/`<` are exact beyond 2^53, a shift count outside 0..63 is an error, an out-of-range literal is a compile error, `random64` gives 64 bits and `time` does not stop at 2038 on Windows. `str_int_limit` returns a constant 2^62. `.ru` VM ABI 3. Tests: `tests/int64.su` (also a `.ru` round trip).
- **Breaking:** variables assigned inside a function are local to the call unless declared with `global(_)`. Programs whose functions share state through undeclared variables need a `global(_): ...` line (the compiler warnings name the variables) or `global(_): "*"`. In a program with `init`, top-level commands still do not run, so state set up in `init` must be declared global. Mbl, russia.su (with uuasm.su) and Tula's DXF error flag were migrated this way; Tver and the libraries needed nothing.
- `.ru` format 2, VM ABI 2: functions carry their local names, `LOAD_VAR`/`STORE_VAR` their resolved scope. Format 1 files are rejected; rebuild them.
- Tver and Tula are now written in USSR (`src/tver/tver.su`, `src/tula/tula.su`); their C sources were removed. Petersburg stays native.

### Fixed
- `math.su`: `tan`/`ctg` guards used `==` instead of `=`.
- REPL no longer hangs reading redirected stdin on Windows.
- REPL on Linux and macOS prints the `PS1`/`PS2` prompts for piped input too, as on Windows and as documented.
- An unexpected character no longer ends the program silently at that point: the lexer reported it but the parser treated it as end of input and ran the rest.
- `break` or `continue` outside any loop is a compile error; at the top level they jumped back to the start of the program and looped until the step limit.
- Line numbers in syntax errors restarted from the previous parse (load, eval, the REPL) and counted lines removed by the preprocessor.
- Petersburg read a freed widget when a window with children was destroyed (crash when a window closed).
- Unused `set(pi)` statements removed from `cos` and `atan` in `math.su`.
- The manual listed `==` as the equality operator; it is `=`.
- `finv_sqrt` works again: the bytecode VM had no route to it, so every call failed.
- `ussr-ubuntu.mk`: `make -j` no longer runs bison twice in parallel.
- `tests/duffs_device.su` is valid USSR now (unrolled `do`/`loop` with `choose` fall-through); `tests/math/*.su`, `tests/lib_io.su`, `tests/lib_math_bytes.su` and `tests/ln_probe.su` include the libraries by their path relative to the test, so they run from any directory.

### Documentation
- `docs/RU_BYTECODE_FORMAT.md`: `.ru` layout, validation and versioning.
- User manual: `-c`/`.ru`, prompts, environment variables, vectors; READMEs and changelogs for Tver, Tula and Petersburg.

### Maintenance
- Test runners: `tests/ru`, `tests/mbl`, `tests/vectors`, `tests/repl`, `tests/russia`, GUI tests in `tests/gui`, `tests/matrix_mul.su`, `tests/env_test.su`, `tests/lib_strings.su`.

## [0.4.7] - 2026-10-20

### Added

Added UUASM assembler layer for USSR VM
Added basic non-crypto sockets support
Added ukraine.su HTTPd and web framework
Added ural.su native package manager
Added tver.su a two-panel file manager
Added tula.su general-purpose 2D CAD application  
Added petersburg.su cross-platform GUI/widget layer for USSR
Added pobeda.ttf monospace font for USSR programmers 

### Documentation

Documented new sub-projects
Documented UUASM spec

### Maintenance

Added test ukraine web app
Added first USSR packages server URL https://tpmme.me/USSR

## [0.4.6] - 2026-10-12

### Added
- **Core Scheduler & Multithreading:**
  - Added a cooperative multithreading scheduler modelled on the FreeBSD kernel featuring a run queue, timer event queue, and quantum-based preemption at instruction boundaries (written from scratch without pthreads or libevent/libev).
  - Added `async(p): name args...` and `await(r): p` commands for spawning and joining scheduled processes with fork-style variable copying.
  - Added `sleep(_): ms` and `yield(_): 0` commands for timer events and voluntary context switching.
  - Added `process(_): name` definition marks and `load(p): "file.su" ["entry"]` execution levels for running files as isolated scheduled processes.
  - Added fork-exec integration ensuring external system commands only block the calling scheduled process via the event queue.
- **Runtime & VM Core:**
  - Added an incremental garbage collector along with the explicit `gc(_): 0` command.
  - Added `elif` / `else` conditional block chains as plain executable commands.
  - Added native `io.su` stream primitives to the `russia.su` bootstrap VM: `open_file`, `close_file`, `write_file`, and `read_file` supporting standard C-style modes (`r`, `w`, `a`, `rb`, `wb`, `ab` and `+` variants).
  - Added `bytes.su` performance optimizations (`set_bytes` / `clear_bytes` memset analogs) directly to the bootstrap VM.
- **Standard Library Builtins:**
  - `io.su`: `open_file`, `close_file`, `write_file`, `read_file`
  - `standard.su`: `random64`, `seed_random64`, `time`
  - `math.su`: `sin`, `ln`, `M_PI`, `M_E`
  - `bytes.su`: `set_bytes`, `clear_bytes`
- **Testing:**
  - Added the `boot_lib.su` bootstrap test.
  - Extended `lib_io.su` to cover file appending, binary modes, and counted-read boundaries.
  - Added `sin ln exp cos tan ctg abs pow ceil floor asin acos atan atan2 grcirc_dist log2 log10 hypot cbrt` commands to `math.su`
  - Added a canonical TextMate grammar at highlighters/TextMate/ussr.tmLanguage.json covering the 0.4.6+ surface (scheduler commands, io/bytes/math primitives, `$if`/`defined`, `M_PI`/`M_E`, hash sigils); the tree-sitter copy in highlighters/tree-sitter/syntaxes/ is updated to match
  
### Changed
- **Breaking API Change:** `set_bytes(_): value vec` and `clear_bytes(_): vec` now fill the entire target vector (memset-style notation). The legacy `vec value count` and `vec count` parameter footprints have been fully removed.
- **System Renames:** Re-branded the bootstrap subsystem from `ussr.su` to `russia.su` and the native built-in editor from `vibe.su` to `Mbl.su`.

### Fixed
- **Math Library:** Fixed critical accuracy degradation in `math.su` `ln`. The previous degree-12 polynomial approximation contained a broken constant term (causing `ln(1)` to yield -3.1 and `ln(e)` to yield -0.33). Replaced with a mathematically rigorous atanh series `2*(z + z^3/3 + z^5/5 + ...)` paired with range reduction, restoring full double-precision accuracy.
- **Stability & Memory Fixes:** 
  - Resolved an ASAN-verified use-after-free vulnerability triggered when spawning external commands from within active scheduled processes.
  - Fixed a routing bug where asynchronous external-command execution results failed to reach their target return variable.
  - Fixed negative integers printing bug

### Documentation
- Completed comprehensive user manual updates covering the new cooperative scheduler architecture, `async`/`await` primitives, process hierarchy, memory management (`gc`), sequential conditional chains, and all core library modules (`io` / `bytes` / `math` / `standard`).

### Maintenance
- Added extensive validation suites: `async.su`, `process_load.su`, `gc.su`, `lib_io.su`, `lib_math_bytes.su`, `boot_async.su`, `boot_load.su`, and `boot_load_entry.su`.
- Completely refactored `conditionals.su` to leverage the new `elif` syntax.
- Added Duff's device analog test
- Added winner quine test
- Added pobeda.ttf font for casual USSR programming

## [0.4.5] - 2026-10-5

### Added
- Added advaced control blocks syntax `@[]`
- Added Kimi K3 `ussr.su` bootstrap attempt
- Added TreeSitter grammar
- Added parts of library math.su bytes.su io.su standard.su
- Added vibe.su builtin text editor
 
### Documentation
- Upadated docs

### Maintenance
- Fixed failing tests
- Added chain.su test

## [0.4.4] - 2026-09-28

### Added
- Added basic autocomplete in REPL mode
- Added template output(Hello 99 Bottles of Beer)
- Added `for(_): {i < MAX}` loops
- Added `else (_): elif(_): choose(_): option(_): default(_): break(_):` conditional constructions
 
### Documentation
- Uploaded some tests as [Rosetta Code](https://rosettacode.org/wiki/Category:USSR) tasks

### Maintenance
- Added 99bottles.su fibs.su fac.su euclid.su and 100doors.su tests
- Fixed failing tests

## [0.4.3] - 2026-09-22

### Added
- Added random64 seed_random64 scan get time builtins
- Added chaining and redirecting output to file or named pipe
- Added script entry points
- Added cd builtin and basic shell PWD manipulation
- Added quadratic equation test
- Added do(_): loop(_): {cond}
### Documentation
- Updated documentation

### Maintenance
- Fix failing tests
- Added Notepad++ syntax highlighter
- Added VIM syntax highlighter

## [0.4.2] - 2026-09-16

### Added
- Added basic programming language facilities.

### Documentation
- Added user manual VM and OOP specifications

### Maintenance
First release

- Adopted the Conventional Commits specification.
- Added CHANGELOG.md