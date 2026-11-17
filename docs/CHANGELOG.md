# Changelog

## 0.2.0
- Refine pass 1:
  - `docs/tools/docgen.su` writes `BUILTINS.md` from `src/help.c`'s help
    table, `LIBRARY.md` from the header comments of `src/libs/*.su`,
    `ROADMAP.md` from every REFINE.md, and `REPOSITORY_MAP.md` from every
    `box.uno` (with what each package is needed by).
  - It also checks: `check` (BUILTINS.md is current), `links` (relative
    links in `docs/` and the package READMEs, with the case checked as on
    Linux) and `coverage` (builtins without a help entry).
  - New guides: `README.md` (an index), `GLOSSARY.md`, `CONTRIBUTING.md`
    (the subproject layout, tests that pass on every machine, code
    conventions) and `PACKAGING.md` (`box.uno`, version constraints,
    Ural).
  - `tests/docs_test.su` runs `check` and `links` in CI.
  - `box.uno` lists the files, and its entry `README.md` now exists.

## 0.1.0
- The manuals and specifications: the user manual, the VM, bytecode,
  `.ru` and UUASM specifications, the optimisation and UNO design notes,
  the grammar, and the bootstrap notes.
