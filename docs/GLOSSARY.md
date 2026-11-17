# Glossary

**Command** — the one form of every statement: `name(result): arguments`.
`set(x): 5` stores, `print(_): x` prints (`_` throws the result away).
See the [manual, chapter 2](USSR_USER_MANUAL.md#2-the-core-idea-everything-is-a-command).

**Block** — `[ ... ]`, a list of commands: the body of a function, `if`
or `while`.

**Expression** — `{ ... }`, the one place infix operators work:
`set(y): {x * 2 + 1}`.

**UNO** — the text form of every USSR value: numbers, strings in
backquotes, `[vectors]`, `Struct{field=...}`. `encode` and `decode`
convert to and from it. `box.uno` manifests and Belarus metadata blocks
are UNO. See [the design notes](USSR-oop-vectors-uno-design.md).

**btree** — the ordered map builtin (`btree`, `btput`, `btget`,
`btfloor`, ...). It is shared by reference, so it is how processes and
`try` hand values back.

**`.su`** — USSR source.

**`.ru`** — USSR bytecode: what `ussr -c` and the usa C compiler write,
and what the VM runs. See [RU_BYTECODE_FORMAT.md](RU_BYTECODE_FORMAT.md).

**UUASM** — the assembly text of the bytecode. See
[USSR_UUASM_SPEC.md](USSR_UUASM_SPEC.md).

**VM** — the bytecode machine that runs `.ru` files. See
[USSR_VM_SPEC.md](USSR_VM_SPEC.md).

**russia.su** — USSR written in USSR (`src/russia`). `tests/run_all.sh`
runs every test through it as well, so any difference from `ussr` shows
up as a failure.

**M1–M5** — the milestones of bootstrapping USSR in itself; see
[BOOTSTRAP_M5.md](BOOTSTRAP_M5.md) and
[BOOTSTRAP_SELFHOST.md](BOOTSTRAP_SELFHOST.md).

**Process** — a function run by the scheduler next to the others
(`async`, `process`, `load`). It gets copies of the globals and shares
btrees by reference.

**ROUND stack** — the web packages that work together: **R**yazan (TLS),
**O**msk (images), **U**kraine (HTTP), **N**ovgorod (the forum) and
**D**ubna (the key-value store).

**Town names** — each package is named after a town or country:
Petersburg (the GUI), Moscow (the kernel and OS), Kremlin (the desktop),
Belarus (the file system), Stalin (the partition editor), Ural (the
package manager), usa (the C compiler), Sochi (Preferans), Mbl (the
editor), Tver (the file manager), Tula (CAD), Siberia (the spreadsheet),
Novosibirsk (documents), Klin, Lenin, KGB, Murmansk and the others in
[REPOSITORY_MAP.md](REPOSITORY_MAP.md).

**`box.uno`** — a package manifest; see [PACKAGING.md](PACKAGING.md).

**`.ural`** — a package archive, a ZIP file.

**`ussr.boxes`** — a repository's package index (SQLite). Ural keeps a
local one too.

**REFINE.md** — a package's list of ideas. A refine pass implements some
of them and marks them `[x]` (done) or `[~]` (partly done); see
[ROADMAP.md](ROADMAP.md).

**Golden file** — `tests/expected/<test>.txt`: the stdout, stderr and
exit status a test must reproduce.
