# USSR programming language CHANGELOG

## [Unreleased]

### Added

### Changed

### Fixed

### Documentation

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
  - Added `sin ln exp cos tan ctg abs pow ceil floor asin acos atan atan2 grcirc_dist cbrt` commands to `math.su`
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