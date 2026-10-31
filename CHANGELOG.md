# USSR programming language CHANGELOG

## [Unreleased]

### Added
- Omsk (`src/omsk/omsk.su`), image thumbnails in pure USSR: PNG (every
  colour type and depth) and sequential JPEG in, a baseline JPEG fitted
  into a square box out. The RUND stack becomes the ROUND stack: Ryazan,
  Omsk, Ukraine, Novgorod, Dubna.

### Changed
- Siberia is split in two: `src/siberia/siberia.su` is the spreadsheet
  and the document editor (`siberia.su --pad` before) is Novosibirsk,
  `src/novosibirsk/novosibirsk.su`, which includes Siberia for the shared
  parts. The pad's engine checks moved to `tests/novosibirsk_test.su`.
- Novgorod makes its 200x200 thumbnails on the server with Omsk, into
  `thumbs/`, and takes the image size from the picture itself; the footer
  links the ROUND stack to the USSR repository.
- `tests/run_all.sh` no longer runs the Stalin test (it writes partition
  tables) or usa's (usa is tested its own way).
- `src/Makefile.am` ships every file of every `.su` package in `make
  dist` (Novgorod's deploy kit, Ukraine, Dubna, Omsk, Sochi and usa were
  missing or only their `box.uno`); `Makefile.msc` and `ussr-ubuntu.mk`
  list every header each object includes, so editing one rebuilds them.

## [0.4.9] - 2026-10-29

The second wave of the project is finished: every subproject listed in
the README has code, and with usa, the C compiler, USSR is complete.

### Added
- usa (`src/usa/usa.su`), a C compiler written in USSR: C99 in, `.ru`
  bytecode out. A preprocessor forked from russia's `pp.su` with `#`
  directives (`#if` expressions, `#`/`##`, variadic macros), a
  hand-written lexer and recursive-descent parser, a code generator for
  the VM, a linker and the `.ru` writer, and a C library written in C
  (stdio, printf/scanf with exact doubles, stdlib, string, math, time,
  getopt). Builds pikchr (its 102 tests render as with gcc), lemon (it
  regenerates pikchr's parser byte for byte), tpm (its test battery
  passes), prng_xrp and LINPACK.
- Sochi, three-player Preferans over UDP with LAN discovery and chat, on
  new `socket_udp`, `socket_sendto`, `socket_recvfrom` and
  `terminal_key_ready` builtins.
- Stalin, the GPT/MBR partition editor, on the block I/O builtins.
- KGB, the Knowledge Giga Base; Siberia, the office package (spreadsheet
  and `--pad` document editor).
- The RUND stack: Ryazan (TLS for ukraine and `prompt`), Novgorod (the
  image forum, with `--prefix` and a deploy kit) and Dubna (the
  PostgreSQL client).
- Kremlin, the full-screen desktop, and Klin, the calculator.
- `launch` and `localtime` builtins, Petersburg full screen, double-click
  and menu events, scaled text; hex integer literals (`0xCAFEBABE 0xDEADBEEF`).

### Changed
- The optimization cycle (docs/OPTIMIZATIONS.md): getf/at/setf/len fast
  paths, the integer fast path, shared reference-counted strings, O(1)
  string length, whole-program optimization in the builds, and russia.su's
  fused compare-and-branch and ordered dispatch.
- CI runs quick tests on pull requests and the full `run_all.sh` on
  releases; releases install winflexbison from GitHub.

### Fixed
- The VM's `LOAD_VAR` leaked its register's old value.

## [0.4.8] - 2026-10-22

### Added
- `bread(v): dev lba [count [sector]]` and `bwrite(n): dev lba bytes [sector]` read and write whole sectors of a disk image or raw disk (`\\.\PhysicalDriveN`, `/dev/sdX`) as byte vectors; `bsize(n): dev` sizes one and `bdevices(v):` lists the storage devices with path, size, kind (HDD, SSD, flash, virtual), bus, model and sector size (Windows storage IOCTLs, Linux `/sys/block`). `crc32(n): data [from [count]]` joins the hashes. Mirrored in russia.su.
- Stalin (`src/stalin/stalin.su`), the partition editor: with no command it lists the storage devices; `show`, `mkimg`, `init gpt|mbr`, `add`, `rm`, `edit`, `wipe` and `types` read and write GPT (both headers and arrays, CRCs, UTF-16 names, attributes, 512 and 4096-byte sectors) and MBR tables (boot flag, CHS, extended partitions with their logical ones read), and recognise ext2/3/4, NTFS, UFS1, UFS2/FFS2 (also inside a BSD disklabel), FAT, exFAT, XFS, Btrfs and swap. `--dry-run` shows the sectors a command would write; raw disks need `--yes`. Checked against util-linux `sfdisk --verify`, `fdisk` and `blkid` on a 20 MB image. Test: `tests/stalin_test.su`.
- `keybind(_): "Mod+F1" "help(_):"` binds a REPL key (Ctrl/Alt/Shift with a..z, 0..9, F1..F12; Mod is Alt) to a line that runs when the key is pressed; one argument reads a binding, none lists them, an empty line unbinds. Works in bestline (xterm, VT220, rxvt, Linux console sequences), the Windows console editor and russia.su's REPL, which on a terminal now reads lines in raw mode (echo, Backspace by UTF-8 character, Ctrl-C, Ctrl-D/Ctrl-Z) and asks the new `keybind_match(line): bytes` builtin about each key. On Windows `terminal_getch` delivers F1–F12 (with modifiers) and Alt chords as xterm bytes instead of dropping them. Test: `tests/keybind.su`.
- `prompt(answer): "To be or not to be?"` asks an AI model through the `curl` program, Anthropic Messages or OpenAI-compatible Chat Completions, configured with `USSR_AI_PROVIDER`, `USSR_AI_URL`, `USSR_AI_MODEL`, `USSR_AI_KEY` and friends; `prompt(_)` prints the answer. The key never appears on a command line. Test: `tests/prompt.su` with canned responses in `tests/ai/`.
- `alias(_): "ll" "ls" "-l"`: `ll(_): x` then runs `ls(_): "-l" x`; the target may be a program, builtin, function or another alias. Expanded in lines typed at the REPL only (ussr's and russia.su's), never in scripts. One argument returns the words, none lists them, `""` removes one. Tests: `tests/shell/run_shell_tests.sh`.
- `moscow.su` from the home folder (`HOME`, else `USERPROFILE`) runs before the current folder's; `USSR_MOSCOW` names the only startup file to run, or none when empty. `tests/run_all.sh` points `HOME` at an empty folder so a personal `moscow.su` cannot change results.
- `try(failed): "function" arg ...` calls a function and catches an error in it: the call ends, the program carries on, `failed` is 1 (the error is still reported), else 0. Test: `tests/try.su`. Not yet in scripts run by russia.su.
- russia.su's REPL carries on after a runtime error in an entry, as ussr's does, and reports it with ussr's two lines (`process N (init) failed`); it runs each entry through `try`. Tests: `tests/repl/run_repl_tests.sh`.
- REPL history file `~/.ussr_history` (`USSR_HISTFILE`, empty for none; `USSR_HISTSIZE`, default 1000): loaded at start into Up/Down history and history-based completion, each typed line appended at once, trimmed on start; only when stdin is a terminal. Shared by bestline, worstline and russia.su's REPL, which also gains Up/Down history.
- `help(_):` lists the builtins; `help(_): "name"` shows one command's usage; topics `syntax`, `keys`, `ai`.
- `ussr -c script.su [out.ru]` (`--compile`) compiles to a `.ru` bytecode file without running it; `ussr program.ru` validates and runs it (signature, format and VM ABI versions, CRC-32, bounds). Blocks passed to commands (`@` chaining) are stored as syntax (format 3) and run as from source; `tests/ru/blocks.su` round-trips one.
- Built-in `Vec2`/`Vec3` struct types with `+ -`, scaling by `* /` and `= !=` in expressions; `src/libs/vectors.su` adds `vdot`, `vcross`, `vlen`, `vnorm`, `vlerp` and more.
- `getenv`, `setenv` and `unsetenv` builtins (UTF-8 on Windows).
- `pop(item): v` removes and returns a vector's last item (error on an empty vector); tests in `tests/oop/vector_pop.su`.
- REPL prompts from `PS1` and `PS2` with `\u \h \w \W \v \$ \\` escapes; piped stdin works on every platform.
- Builtins for apps written in USSR: `list_dir`, `is_dir`, `getcwd`, `make_dir`, `rename_path`, `remove_file`, `remove_dir`, `read_line`, `typeof`, `fail`, `chr`.
- `copy_file(ok): from to` copies one file byte for byte (0 ok, 1 failed); it never replaces an existing destination and refuses directories. Test: `tests/copy_file.su`.
- Preprocessor `$once` include guard and `__MAIN__` for files that are both a library and a program.
- `src/libs/strings.su` (number/text conversion, UTF-8 helpers) and `src/libs/vectors.su`.
- Mbl: UTF-8 (Russian) editing by character, count prefixes, `w`, `cw`, and multi-level undo `u` / redo `Ctrl+R`.
- Tver: directory selection with Insert, held-arrow repeat, direct launch with `ussr src/tver/tver.su`, F5 copy (directories recursively) and F7 new directory.
- Tula: DXF read/write, zoom, adaptive grid, toolbar, direct launch with `ussr src/tula/tula.su`.
- Petersburg: canvas and toolbar widgets, `keyup`/`focus`/`wheel` events, event injection for tests, timers while the GUI runs.
- russia.su assembles and runs textual UUASM (`.ua`).
- Continuous integration (`.github/workflows/ci.yml`): Ubuntu with GLFW under Xvfb, Ubuntu without GLFW, and Windows (MSVC, GLFW 3.5.1) build and run every test on pull requests and pushes to `main`.
- Function-local variables: every call has its own parameters, return variable and every variable the function assigns, so helpers and recursion no longer overwrite each other's temporaries (before, `fib(a): m1` / `fib(b): m2` style recursion computed `fib(20)` as 1). Names a function only reads are globals. `global(_): names` declares shared state, at the top level for every function or inside one function; `global(_): "*"` restores the old behavior. `eval` inside a function uses its locals; processes started in a function see them as globals. The compiler warns (`file:line: warning:`) about reading a local before setting it, setting a local only other code reads, and reading a global that is only ever set as a local. Variable accesses inside functions are resolved at compile time. See "Variables in functions" in the manual; tests in `tests/locals.su`, `tests/locals_legacy.su`.
- Compile errors say where and why: `file:line: reason`, with the original file and line also inside `$include`d files (the preprocessor keeps a line map). Every compiler failure path reports its command; syntax errors and bad characters use the same format; the REPL reports `<stdin>:N`, `eval` text `<eval>:N`. `tests/errors/run_error_tests.sh` checks 22 bad programs.
- `tests/run_all.sh` runs every suite, checks each standalone test script against `tests/expected/` (stdout, stderr, exit status) and runs the GUI tests; `--update` rewrites the expectations.
- Preprocessor `$elif defined(NAME)` (also `defined NAME` without parentheses) for `$if`/`$ifdef` chains; the first true branch wins, `$else` only when none did. Test: `tests/pp_elif.su`.
- Ural icon `src/ural/ural.ico`, generated by `src/ural/icon.py`.
- Pobeda 3.5: marked fixed pitch, plain `Pobeda` family name, consistent line metrics, box drawing/blocks/shades, Russian punctuation (`«» „“ № – …`), smaller and seam-free outlines (see `fonts/README.txt`).

### Changed
- `src/parser.y` has no bison conflicts (was 3 shift/reduce, 40 reduce/reduce): a newline is its own empty command line, and inside `{}` a negative number is unary minus folded back into a literal. `docs/grammar.bnf` and the Vim, Notepad++, TextMate and tree-sitter highlighters follow the current lexer and parser.
- Integers are 64-bit on every platform (they were C `long`, 32-bit on Windows: a literal like 3000000000000 was cut to 32 bits, so `tests/math/test_log_hypot.su` printed 3.037e+09 instead of 5e+12, and overflow hung `str_from_number`). `+ - *` wrap around instead of being undefined, `x % -1` no longer traps, integer `=`/`<` are exact beyond 2^53, a shift count outside 0..63 is an error, an out-of-range literal is a compile error, `random64` gives 64 bits and `time` does not stop at 2038 on Windows. `str_int_limit` returns a constant 2^62. `.ru` VM ABI 3. Tests: `tests/int64.su` (also a `.ru` round trip).
- **Breaking:** variables assigned inside a function are local to the call unless declared with `global(_)`. Programs whose functions share state through undeclared variables need a `global(_): ...` line (the compiler warnings name the variables) or `global(_): "*"`. In a program with `init`, top-level commands still do not run, so state set up in `init` must be declared global. Mbl, russia.su (with uuasm.su) and Tula's DXF error flag were migrated this way; Tver and the libraries needed nothing.
- Mbl's undo and redo histories are stacks built on `pop` instead of vectors with separate size counters.
- `.ru` format 2, VM ABI 2: functions carry their local names, `LOAD_VAR`/`STORE_VAR` their resolved scope. Format 1 files are rejected; rebuild them.
- Tver and Tula are now written in USSR (`src/tver/tver.su`, `src/tula/tula.su`); their C sources were removed. Petersburg stays native.

### Fixed
- Output order: `ussr` flushes its stdout before starting a program, so a program's output no longer comes out ahead of what was printed before it when stdout is a pipe or file.
- russia.su's REPL passed `null` for every argument of an external program (`echo(_): "x"` printed `null`); scripts were not affected.
- Windows `ussr_process_run`: the temporary files for a child's stdin and captured stdout were opened non-inheritable, so the child never saw them (no caller used either until `prompt`).
- `math.su`: `tan`/`ctg` guards used `==` instead of `=`.
- REPL no longer hangs reading redirected stdin on Windows.
- REPL on Linux and macOS prints the `PS1`/`PS2` prompts for piped input too, as on Windows and as documented.
- `init`'s return value is the exit status: 0..255 are passed through (they all became 1), other integers give 1 (they gave 0). `-e` errors exit with 1 instead of 255. `tests/exit/run_exit_tests.sh` covers source, `.ru` and `-e`.
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