# Contributing to USSR

How to build USSR, test it and add to it. Building and the test runners
are covered in the [user manual, chapter 16](USSR_USER_MANUAL.md#16-building-and-testing-ussr);
this page is the part around it: the layout of a subproject, tests that
pass on every machine, and the conventions the code follows.

## The repository

[REPOSITORY_MAP.md](REPOSITORY_MAP.md) lists every package with what it
is and what it depends on. The interpreter is the C in `src/` (`parser.y`,
`lexer.l`, `ussr.c`, `main.c`, the `ussr_oop_*.c` builtins). Each folder
under `src/` with a `box.uno` is a package written in USSR. `tests/` holds
the test scripts and `tests/expected/` their recorded output.

## A subproject

The packages share one layout. `ural new NAME` makes it (see
[PACKAGING.md](PACKAGING.md)):

| File | What |
|------|------|
| `box.uno` | the manifest Ural installs from |
| `NAME.su` | the entry: a library plus, under `$ifdef __MAIN__`, a command line |
| `README.md` | what it does and how to use it |
| `CHANGELOG.md` | one `## VERSION` section per release, newest first |
| `REFINE.md` | ideas for the next passes; `[x]` done, `[~]` partly done |
| `docs/API.md`, `examples/`, `tests/TESTPLAN.md` | where they apply |

When a change lands, add it to the CHANGELOG under a new version, bump
`version` in `box.uno` (`ural version minor`), and mark what it finished in
REFINE.md. [ROADMAP.md](ROADMAP.md) counts those marks.

## Tests

A test is a USSR script in `tests/` whose stdout, stderr and exit status
must match `tests/expected/<script>.txt`. Write the script, check its
output by hand, then record it with `sh tests/run_all.sh --update` (and
review the diff before committing). CI runs the quick set
(`tests/run_default.sh`) on Linux for every pull request, so a test has
to give the same output on every machine:

- **Seed randomness.** `random64` is USSR's own generator, so after
  `seed_random64(_): N` it gives the same numbers on every platform. A
  test that prints a UUID, a GUID or a shuffle must seed it, or use the
  program's own seed option (`STALIN_SEED`, `sochi --seed`).
- **Fix the time.** Print no clock values, or set the program's switch
  for a fixed time (`BELARUS_TIME`).
- **Sort directory listings.** `list_dir` gives the host's order, and
  some hosts mark folders with a trailing `/`.
- **Write files in `tests/` with a test-specific prefix and remove them
  at the end**, so two tests never share a scratch file.
- **Keep it fast.** A loop that pushes bytes one at a time takes minutes
  over megabytes. Move data with `bread`, `bwrite` and `bcopy`, which
  copy in C.
- **Use LF line endings.** The expected files are compared with `\r`
  removed, but the sources and docs in the repository use LF.

Tests that cannot run unattended are skipped by name in
`tests/run_all.sh`: the socket tests, Stalin (it writes partition tables)
and usa (see `src/usa/README.md`). Run those by hand.

## Code conventions

- **Prefix every function and global of a package** with the package's
  short name (`st_` Stalin, `bl_` Belarus, `so_` Sochi, `blm_` and `stm_`
  for their refine-pass files).
- **Pick local names that cannot clash.** A top-level `global(_): X` in a
  program makes `X` global in every function, library functions too, so
  a library's local `X` would overwrite it. Tests use prefixed globals
  for the same reason.
- **Return a status and print the reason.** Commands return 0 for done,
  1 for nothing done or declined, and 2 for a usage or device error,
  and say why on stderr.
- **Keep state that may be missing in a btree.** Reading a missing hash
  variable (`name?`) is fatal even under `try`, while
  `btget(v): t key default` gives the default.
- **Get results out of `try` through a btree.** `try` gives no result
  back, so pass a btree, which is shared by reference, and read the
  value from it.
- **Put new features in a file of their own.** Packages that grew in a
  refine pass put the new code in `more.su` (included from the entry),
  so the original file stays readable.

## Commits and pull requests

One pull request per subproject or fix, from a branch off `main`. CI must
pass: when it fails, look at the job's log for the `FAIL` line and the
diff under it.

## Generated documents

`docs/BUILTINS.md`, `docs/LIBRARY.md`, `docs/ROADMAP.md` and
`docs/REPOSITORY_MAP.md` are written by `docs/tools/docgen.su`; do not edit
them by hand. `ussr docs/tools/docgen.su check` and `links` are what
`tests/docs_test.su` runs.
