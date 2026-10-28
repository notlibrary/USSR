<p align="center">
   <img src="https://img.shields.io/github/license/notlibrary/USSR" alt="License	">
   <img src="https://img.shields.io/github/manifest-json/v/notlibrary/USSR" alt="Version">
   <img src="https://img.shields.io/github/actions/workflow/status/notlibrary/USSR/release.yml" alt="Build">
</p>

# USSR Unified Shell Script REPL
Programming Language with unique grammar resembling TCL and Conventional Commits
Designed by me coded with ChatGPT after accidental prompt

>delegate syntax; retain responsibility.

I share 0.1 version with `hello.su` first program in it

Perhaps it evolves fast to something big No promises or insight unfortunately

.su file extension means "script user" not that you think

The language/toolchain uses three file extensions: `.su` is USSR source,
`.ua` is textual UUASM (USSR Ukraine assembler), and `.ru` is the intended
final VM binary bytecode. The CLI runs all three; `.uuasm` is retained
as a compatibility suffix for assembler source. `ussr -c script.su [out.ru]`
compiles without running, and `ussr out.ru` runs the result (format:
[docs/RU_BYTECODE_FORMAT.md](docs/RU_BYTECODE_FORMAT.md)).

```USSR
	#ЗДОРОВО МОРЯЧОК
	emerge(report): "K-152"
	print(aye): "BOOM!"
```

The actual list of epithets are:

- General Purpose
- Scripted 
- Multi-paradigm 
- Interpreted
- Object oriented
- Open source
- Free 
- Dynamically typed
- Shell
- Preprocessed 
- Imperative
- Functional
- Self-referencing
- Partially AI designed
- Command
- Virtually executed
- Portable
- Multiplatform

The language is deliberately designed for working with low-quality code *en masse* with zero equipment
You can shout it commands without a pencil standing on construction site fab or in military or city crowd and be understood

Because of AI development we achieved near impossible performance USSR interpreter written in USSR russia.su is up
in less than a man month there are even own vi clone Mbl.su (vi->wi->mi->Mbl quality is mediocre but nobody cares
the whole thing is running forward like a steam engine now it's all about quantity now how much tests lines of code rosetta tasks interpreted commands gigabytes
and other statistical parameters you can imagine

The project uses real existing geographical toponyms or cultural significant objects to name it's scripts 
e.g. tula.su - 2D CAD ural.su - package manager ukraine.su - httpd daemon and framework tver.su - file manager russia.su - bootstrapped interpreter petersburg.su - gui library 
moscow.su - main USSR configuration file etc
few exceptions are Mbl.su - text editor and no-name std library with shared functions package called library

Newer subprojects (each has a `box.uno` in `src/<name>/`; Klin, Ryazan, Kremlin, Novgorod, Dubna, Siberia and KGB have code so far):

- dubna.su - PostgreSQL client: protocol 3.0, TLS, SCRAM-SHA-256, parameterised queries (working: `src/dubna`)
- kgb.su - Knowledge Giga Base, fact-oriented Markdown knowledge base: folders are objects and categories (working: `src/kgb`)
- klin.su - GUI calculator, programmer, scientific and casual in one (working: `src/klin`)
- kremlin.su - GUI desktop manager, a full-screen desktop on ~/kremlin (working: `src/kremlin`)
- novgorod.su - anonymous HTTPS image forum for programmers with lightweight markup (working: `src/novgorod`)
- ryazan.su - secure socket library: TLS and HTTPS on Schannel or OpenSSL (working: `src/ryazan`; ukraine.su serves HTTPS and `prompt(_):` reaches AI providers through it)
- siberia.su - office package, spreadsheet tables and document editor
- stalin.su - hard disk formatting manager
- usa.su - C compiler written in USSR
- sochi.su - desktop preference game

The **RUND stack** is the project's mainline: Ryazan (TLS) under Ukraine
(HTTP) under Novgorod (the forum), with Dubna (the PostgreSQL client) under
Novgorod's `--db`.

```bash
ussr src/novgorod/novgorod.su          # Novgorod on https://localhost:8443/
ussr src/novgorod/novgorod.su --db "postgres://forum:secret@localhost/forum"
```

```USSR
$include "ural.su"
instsall(code): "ukraine russia"
```

or(if you prefer classic shell)

```bash
ural install ukraine russia
```

USSR is a bug-ridden project critical simple system crippling bugs may appear RANDOMLY in any time any place AI won't help much too big context
Examples: it can't print negative numbers else branches is broken e.t.c

USSR is a neuroslopware it's created entirely by prompting an AI followed by enrichment and refining results with the next AI "centrifuge"

<img width="1536" height="1024" alt="USSR Compilation Pipeline Infographic" src="https://github.com/user-attachments/assets/bd7b768f-d02b-4fc3-bde6-009b68e75875" />

Desktop applications run straight from their sources with a GLFW-enabled
build:

```bash
ussr src/tver/tver.su [directory]     # Tver, two-panel file manager
ussr src/tula/tula.su [drawing.dxf]   # Tula, 2D CAD and DXF viewer
ussr src/klin/klin.su [sci|prog]      # Klin, calculator
ussr src/kremlin/kremlin.su [--window] # Kremlin, desktop on ~/kremlin
```

KGB, the knowledge base, runs in the terminal:

```bash
ussr src/kgb/kgb.su init zoo
ussr src/kgb/kgb.su --kb zoo add animals/mammals/cat legs: 4
ussr src/kgb/kgb.su --kb zoo where "legs>2"
```

This file is my personal devlog if you need user manual or more USSR design documents check the docs folder
If you want to add 3rd party USSR package project create a repo then add the box.uno then pack and upload it 
to the trusted mountain(ftp server) using ural 

I am far beyond postneuroslopware coder now I owned the WHOLE LOOP and found literally nothingness in the end on the other side
dreadful emptiness the machine asking for the next prompt the guy with dozens anime-avatar accounts is following me
AI is like a plastic cheap ubiquitous thing that wins the entire world 

P.S. СССР Средство Создания Совершенных Разработок
