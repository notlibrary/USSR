# tests — refinement ideas

**Today:** the regression and conformance collection: about 87 `.su`
tests at the top level plus folders `ai errors exit expected gui math
mbl oop quines repl ru russia ryazan shell ural vectors`.
`tests/run_all.sh` runs everything twice: with native `ussr` and through
`src/russia/russia.su`, where every difference is a parity gap
(`USSR_RUSSIA_STRICT`). Output is compared with `tests/expected/`. GUI
tests are skipped with `USSR_SKIP_GUI=1`. CI workflows are `ci`,
`run-all`, `release` and `version`.

## Coverage
- A test per standard library function (`lib_strings_*`,
  `lib_math_*`, ...) with edge cases (empty, Unicode, limits)
- A builtin coverage report: which native commands no test calls
- Line/branch coverage of the C interpreter (gcov) in CI, with the
  report published
- Tests for every chapter of the user manual: extract and run its
  examples
- Error-path tests for each subproject's CLI (bad arguments, missing
  files), with exit status checked
- Unicode corpus tests: Cyrillic, CJK, emoji, combining marks across
  strings, the REPL and Mbl
- Tests for every AGENTS.md shell feature as it lands (pipes, env,
  bindings, chaining)
- Subprojects with no automated tests yet (Mbltishi, Ural's USSR
  wrapper, Petersburg's widgets beyond the GUI apps) get at least a
  smoke test

## Kinds of tests
- Differential testing: run every test under ussr, russia and the `.ru`
  path (compile then run), comparing all three
- Fuzzing: the lexer and parser, the UNO decoder, `pp`, Novgorod
  markup, Omsk images, Dubna protocol messages, Stalin tables and
  Murmansk rules (libFuzzer on the C side, a USSR fuzz driver for the
  pure-USSR libraries)
- Property-based tests: a small QuickCheck-style library in USSR
  (generators, shrinking)
- Golden image tests for the GUI apps with a headless renderer
- Performance regression tests: benchmark programs with time budgets,
  and a history chart
- Memory tests: run the C interpreter under ASan/UBSan/Valgrind in CI
- Long-running soak tests for the scheduler and servers (Novgorod for
  an hour)
- Network tests (Ryazan, Ukraine, Sochi) with port discovery so they
  can join `run_all.sh`
- Live-service tests (Dubna against PostgreSQL, Lenin against a fake
  provider) in a CI job with containers

## Harness
- A test runner written in USSR (parallel, filters, `--only`, `--list`,
  timing, TAP or JUnit XML output for CI)
- Per-test timeouts and isolation: a scratch `HOME` and current
  directory for every test, so tests cannot touch real user files
- Expected-output update mode (`--bless`) with a diff review
- Skip markers inside test files (`# requires: gui, tls, network`) in
  place of environment switches
- Flaky-test detection: rerun failures and report instability
- Clean up stray fixtures: `boot_lib_tmp.txt` and `novgorod_tmp/` are
  left in the tree today
- Group the top-level `.su` files into folders by area (core, lib,
  net, subprojects)
- A summary badge per area in the README

## CI
- Matrix: Windows (MSVC, MinGW), Ubuntu, macOS, and Moscow in QEMU
- Run the russia pass in a nightly job when it is too slow for every
  push
- Cache builds between jobs
- Upload failing outputs as artifacts

## Test design
- A conformance suite for the language, separate from regression
  tests: one small file per rule of the manual and `grammar.bnf`,
  usable by any future implementation
- Negative tests for every compile-time diagnostic in `check.su`, with
  the exact message
- Boundary tests: 64-bit integer edges, very long strings, deep
  recursion (the 1 024-call limit), large vectors
- Scheduler tests: fairness, sleeping tasks, many tasks, a task that
  fails
- Cross-platform path and line-ending tests (CRLF sources, BOMs,
  drive letters)
- Locale and console-encoding tests on Windows (Cyrillic file names
  and output)
- Upgrade tests: files written by an older release (KV stores, `.ru`
  bytecode, settings) still load
- Install tests: the release archives unpack and run on a clean
  machine
- Example tests: every `examples/*.su` in every package at least
  parses and starts

## Reporting
- A dashboard of the russia parity gap over time
- Per-test timing history, with the slowest tests listed
- A matrix page: test by platform, pass or fail
- Failure triage notes kept next to the expected files
- Mutation testing of the interpreter: change an operator in the C
  source and see whether any test fails

## Fixtures
- A shared fixtures folder with documented sample files (images, DXF,
  FODS, disk images, certificates), in place of each test making its
  own
- Deterministic seeds everywhere, with the seed printed on failure
- Generated fixtures rebuilt by scripts kept in the tree (the JPEGs
  made by Pillow, the test certificate)
- A check that tests leave the working tree clean
