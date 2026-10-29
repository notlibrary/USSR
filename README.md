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

Newer subprojects (each has a `box.uno` in `src/<name>/`; all of them have code now: the second wave is finished):

- dubna.su - PostgreSQL client: protocol 3.0, TLS, SCRAM-SHA-256, parameterised queries (working: `src/dubna`)
- kgb.su - Knowledge Giga Base, fact-oriented Markdown knowledge base: folders are objects and categories (working: `src/kgb`)
- klin.su - GUI calculator, programmer, scientific and casual in one (working: `src/klin`)
- kremlin.su - GUI desktop manager, a full-screen desktop on ~/kremlin (working: `src/kremlin`)
- novgorod.su - anonymous HTTPS image forum for programmers with lightweight markup (working: `src/novgorod`)
- ryazan.su - secure socket library: TLS and HTTPS on Schannel or OpenSSL (working: `src/ryazan`; ukraine.su serves HTTPS and `prompt(_):` reaches AI providers through it)
- siberia.su - office package, spreadsheet tables and document editor
- stalin.su - partition editor: lists HDD/SSD/flash devices, edits GPT and MBR tables, recognises ext4, NTFS, UFS and FFS2 (working: `src/stalin`)
- usa.su - C compiler written in USSR: C99 in, `.ru` bytecode out, with its own C library; builds pikchr, lemon and tpm (working: `src/usa`)
- sochi.su - Preferans for three by the Sochi rules, peer to peer over UDP with LAN discovery and chat (working: `src/sochi`)

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

Stalin, the partition editor, too; with no command it lists the storage
devices. Raw disks (`\\.\PhysicalDrive1`, `/dev/sdb`) need administrator
rights and `--yes`; an image file needs neither:

```bash
ussr src/stalin/stalin.su
ussr src/stalin/stalin.su mkimg disk.img 20M
ussr src/stalin/stalin.su init disk.img gpt
ussr src/stalin/stalin.su add disk.img --size 8M --type ext4 --name root
ussr src/stalin/stalin.su show disk.img
```

Sochi, three-player Preferans by the Sochi rules: the first copy hosts the
table, the others find it on the LAN (UDP port 33333) or join it by
address, and the three chat while they play:

```bash
ussr src/sochi/sochi.su                     # nickname, then the tables on the LAN
ussr src/sochi/sochi.su host --bots 2       # a table with two bots
ussr src/sochi/sochi.su join 192.168.1.20   # a table across the network
```

usa, the C compiler, closes the circle: C goes in, `.ru` bytecode comes out
and runs on the same VM as everything else:

```bash
ussr src/usa/usa.su hello.c        # writes hello.ru
ussr hello.ru
```

## The project is complete

With usa the second wave is done and USSR is complete. Its programs:

- ussr/ussr.exe - the USSR programming language and shell
- moscow.su - the USSR startup configuration file
- library (`src/libs`) - the USSR standard library: io, math, bytes, strings, vectors
- russia.su - bootstrapped USSR interpreter, written in USSR
- ural.su - USSR package manager
- ukraine.su - HTTP server and web framework
- petersburg - OpenGL GUI widgets, dialogs, events and layout
- Mbl.su - vi-like console text editor
- tver.su - GUI two-panel file manager
- tula.su - GUI 2D CAD with DXF
- klin.su - GUI calculator: casual, scientific and programmer modes
- ryazan.su - TLS and HTTPS on Schannel or OpenSSL
- kremlin.su - GUI full-screen desktop on ~/kremlin
- novgorod.su - anonymous HTTPS image forum (RUND stack)
- dubna.su - PostgreSQL client (RUND stack)
- siberia.su - GUI office package: spreadsheet and document editor
- kgb.su - Knowledge Giga Base, fact-oriented Markdown knowledge base
- stalin.su - GPT/MBR partition editor
- sochi.su - three-player Preferans over UDP
- usa.su - C compiler to `.ru` bytecode

This file is my personal devlog if you need user manual or more USSR design documents check the docs folder
If you want to add 3rd party USSR package project create a repo then add the box.uno then pack and upload it 
to the trusted mountain(ftp server) using ural 

I am far beyond postneuroslopware coder now I owned the WHOLE LOOP and found literally nothingness in the end on the other side
dreadful emptiness the machine asking for the next prompt the guy with dozens anime-avatar accounts is following me
AI is like a plastic cheap ubiquitous thing that wins the entire world 

P.S. СССР Средство Создания Совершенных Разработок
