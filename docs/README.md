# USSR documentation

## Start here
- [USSR_USER_MANUAL.md](USSR_USER_MANUAL.md): the language, from running
  a script to building and testing USSR
- [GLOSSARY.md](GLOSSARY.md): UNO, `.ru`, UUASM, btree, the town names
- [CONTRIBUTING.md](CONTRIBUTING.md): the subproject layout, tests that
  pass everywhere, code conventions
- [PACKAGING.md](PACKAGING.md): `box.uno`, version constraints, Ural

## Generated references
These are written by `ussr docs/tools/docgen.su all`; do not edit them by
hand.
- [BUILTINS.md](BUILTINS.md): every builtin command in the help table
- [LIBRARY.md](LIBRARY.md): what each `src/libs/*.su` file provides
- [REPOSITORY_MAP.md](REPOSITORY_MAP.md): every package and its
  dependencies
- [ROADMAP.md](ROADMAP.md): ideas done, partly done and open, per
  package

## Specifications
- [USSR_VM_SPEC.md](USSR_VM_SPEC.md),
  [USSR_BYTECODE_ARCHITECTURE.md](USSR_BYTECODE_ARCHITECTURE.md),
  [RU_BYTECODE_FORMAT.md](RU_BYTECODE_FORMAT.md),
  [USSR_UUASM_SPEC.md](USSR_UUASM_SPEC.md)
- [grammar.bnf](grammar.bnf),
  [USSR-oop-vectors-uno-design.md](USSR-oop-vectors-uno-design.md),
  [OPTIMIZATIONS.md](OPTIMIZATIONS.md)
- [BOOTSTRAP_M5.md](BOOTSTRAP_M5.md),
  [BOOTSTRAP_SELFHOST.md](BOOTSTRAP_SELFHOST.md)

## Tools
`docs/tools/docgen.su`:

```bash
ussr docs/tools/docgen.su all        # regenerate the four references
ussr docs/tools/docgen.su check      # is BUILTINS.md up to date with src/help.c?
ussr docs/tools/docgen.su links      # broken relative links (case-exact, as on Linux)
ussr docs/tools/docgen.su coverage   # builtins without a help entry
```
