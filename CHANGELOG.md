# USSR programming language CHANGELOG

## [Unreleased]

### Added

### Documentation

### Maintenance

## [0.4.6] - 2026-10-5

### Added
- Added a cooperative multithreading scheduler modelled on the FreeBSD
  kernel: run queue, timer event queue, quantum-based preemption at
  instruction boundaries, written from scratch (no pthreads, no
  libevent/libev)
- Added `async(p): name args...` / `await(r): p` commands for spawning
  and joining scheduled processes, with fork-style variable copy
- Added `sleep(_): ms` / `yield(_): 0` commands (timer events and
  voluntary context switch)
- Added `process(_): name` definition marks and `load(p): "file.su"
  ["entry"]` for running a file as a separate scheduled process — the
  third init-hierarchy level after REPL outer commands and global init
- Added fork-exec integration: spawning an external command from a
  scheduled process blocks only that process via the event queue
- Added an incremental garbage collector and the `gc(_): 0` command
- Added `elif` / `else` block chains (as plain commands, not grammar)
- Added library builtins: `open_file` `close_file` `write_file`
  `read_file` (io.su), `random64` `seed_random64` `time`
  (standard.su), `sin` `ln` `M_PI` `M_E` (math.su), `set_bytes`
  `clear_bytes` (bytes.su)
- Ported the scheduler, event queue, async/await, process/load and gc
  to the `ussr.su` bootstrap: a pure-USSR cooperative scheduler inside
  the bootstrap bytecode VM with logical-tick timers

### Documentation
- Documented the scheduler, async/await, process/load, gc and
  elif/else chains in the user manual

### Maintenance
- Fixed a use-after-free when spawning external commands from
  scheduled processes (ASAN-verified)
- Fixed async external-command results not reaching the return
  variable
- Added async.su process_load.su gc.su lib_io.su lib_math_bytes.su
  tests plus boot_async.su boot_load.su boot_load_entry.su bootstrap
  scheduler tests; rewrote conditionals.su to use elif

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