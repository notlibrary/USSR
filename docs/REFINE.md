# docs — refinement ideas

**Today:** the manuals and specifications:
- `USSR_USER_MANUAL.md` (16 chapters, from running USSR to building
  and testing)
- `USSR_VM_SPEC.md`, `USSR_BYTECODE_ARCHITECTURE.md`,
  `RU_BYTECODE_FORMAT.md` and `USSR_UUASM_SPEC.md`
- `OPTIMIZATIONS.md`, `USSR-oop-vectors-uno-design.md` and
  `grammar.bnf`
- The bootstrap notes (M5, self-host) and `PROMPTS.md`
- The pipeline infographic and the Moscow and Kremlin GIFs

## New documents
- A tutorial ("USSR in 30 minutes"): from hello world to a small Ukraine
  web app
- A cookbook of short recipes: read a file, parse CSV, HTTP GET, run
  a command, async tasks, structs and UNO
- A standard library reference generated from the `src/libs/*.su`
  header comments
- A builtin reference: every native command with its arguments,
  results and errors, generated from the C tables so it cannot drift
- A shell guide for the AGENTS.md features (aliases, `kiev.su`,
  bindings, pipes, environment), updated as each one lands
- A Petersburg GUI programming guide (callbacks as processes, `uilock`,
  state in the hash namespace), which Tver, Tula, Klin, Siberia and
  Kremlin each re-explain today
- The ROUND stack overview: Ryazan, Omsk, Ukraine, Novgorod and Dubna,
  and how a request flows
- A Moscow (the USSR kernel/OS) architecture document, if it is not
  already covered
- A packaging guide: writing `box.uno`, the subproject layout (README,
  CHANGELOG, docs/API.md, examples, tests/TESTPLAN.md, and now
  REFINE.md) and publishing with Ural
- A contributor guide: the build on each platform, running
  `tests/run_all.sh` (native and russia passes), the expected-output
  files, and code style (prefixes, globals)
- Error message catalogue: every diagnostic with a cause and a fix
- A glossary: UNO, `.ru`, UUASM, M1–M5, ROUND, the town names
- Release notes per version, collected from the subproject CHANGELOGs
- A security model: what the scheduler, external commands, sockets and
  Ural do and do not protect against
- A design rationale ("why everything is a command")

## Specifications
- Bring `grammar.bnf` in line with the parser (generated from
  `parser.y`, or checked by a test)
- A formal semantics section for values, copying and sharing (vectors
  and structs are references)
- A scheduler specification: time slices, fairness, and what blocks
- Versioning rules for `.ru` and UNO compatibility

## Presentation
- A static documentation site (built by a USSR script and served by
  Ukraine, or GitHub Pages)
- Search across all docs
- Diagrams as source (Pikchr — usa already compiles pikchr) so they
  can be regenerated
- Runnable examples: every code block in the manual extracted and run
  in CI
- Translations: a Russian edition of the user manual
- A man page (`ussr.1`) and `--help` text generated from the manual

## Upkeep
- A link checker for internal links across `docs/` and the subproject
  READMEs
- A docs-coverage report: builtins and library functions with no
  documentation
- Move `PROMPTS.md` (1 100 lines) into a history or research folder,
  out of the user-facing docs
