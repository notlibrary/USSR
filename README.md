<p align="center">
   <img src="https://img.shields.io/github/license/notlibrary/USSR" alt="License	">
   <img src="https://img.shields.io/github/manifest-json/v/notlibrary/USSR" alt="Version">
   <img src="https://img.shields.io/github/actions/workflow/status/notlibrary/USSR/release.yml" alt="Build">
</p>

# USSR Unified Shell Script REPL

![Kremlin, the USSR desktop, on Moscow: Mbltishi, the terminal, runs the USSR shell, which prints hello world](docs/images/kremlin.gif)

Programming Language with unique grammar resembling TCL and Conventional Commits
Designed by me coded with ChatGPT after accidental prompt

>delegate syntax; retain responsibility.

I share 0.1 version with `hello.su` first program in it

Perhaps it evolves fast to something big No promises or insight unfortunately
It's 3 in 1 now actually cross-platform shell + widget library programming language + scripts packages and OS + coding agent 

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
kiev.su - main USSR configuration file etc
few exceptions are Mbl.su - text editor and no-name std library with shared functions package called library
install.su - Moscow kernel installer and top level packages fonts tests docs highlight

Newer subprojects (each has a `box.uno` in `src/<name>/`; all of them have code now: the second wave is finished):

- dubna.su - PostgreSQL client: protocol 3.0, TLS, SCRAM-SHA-256, parameterised queries (working: `src/dubna`)
- omsk.su - image thumbnails: PNG and baseline JPEG in, a 200x200 JPEG out, in pure USSR (working: `src/omsk`)
- kgb.su - Knowledge Giga Base, fact-oriented Markdown knowledge base: folders are objects and categories (working: `src/kgb`)
- klin.su - GUI calculator, programmer, scientific and casual in one (working: `src/klin`)
- kremlin.su - GUI desktop manager, a full-screen desktop on ~/kremlin (working: `src/kremlin`)
- novgorod.su - anonymous HTTPS image forum for programmers with lightweight markup (working: `src/novgorod`)
- ryazan.su - secure socket library: TLS and HTTPS on Schannel or OpenSSL (working: `src/ryazan`; ukraine.su serves HTTPS and `prompt(_):` reaches AI providers through it)
- siberia.su - GUI spreadsheet: formulas, flat ODF (.fods) and CSV (working: `src/siberia`)
- novosibirsk.su - GUI document editor with system fonts, split from Siberia: flat ODF text (.fodt) and .txt (working: `src/novosibirsk`)
- Mbltishi.su - the desktop's terminal: the USSR shell in a Petersburg window on Moscow, started from Kremlin (working: `src/Mbltishi`)
- moscow.su - amd64 kernel: a NASM two-stage boot loader starts a NASM USSR runtime (a `.ru` VM on bare metal) that runs moscow.ru, which mounts a Belarus root file system and runs russia.su (the self-hosted interpreter) as its shell with kernel commands like ls(_):, cat(_):, run(_): and install(_):; a terminal console (raw keys, ANSI on VGA) that runs the Mbl editor from its source; an e1000 driver and its own TCP/IP stack (DHCP, ARP, ICMP, TCP) behind the Murmansk firewall that serve Novgorod over HTTP and, with BearSSL loaded as a C library, HTTPS; windows: Petersburg in `/lib/gui.so` on a software OpenGL and a GLFW of its own on the VGA framebuffer, so Kremlin, Mbltishi and Tver run on it, and an installed system boots into the desktop; tasks: `spawn` runs programs side by side, each in an address space of its own (page tables, demand paging, TLSF heaps), switched by the timer; boots from the Belarus install disk, tested in QEMU (working: `src/moscow`)
- belarus.su - the USSR file system: inodes whose block maps are B+ trees from file block index to disk LBA, directories as files of entries; mkfs, ls, put, cat, fsck on disks and images, and Moscow's root (working: `src/belarus`)
- murmansk.su - the firewall of Moscow's network stack: ordered allow/deny rules and policies, stateful, rules in /etc/murmansk.rules, `fw(_):` (working: `src/murmansk`)
- install.su - the USSR installer: `install(_):` in Moscow asks about the disk and partitions, makes a Belarus partition with Stalin and copies USSR onto a virtual disk that then boots (working: `src/moscow/install.su`)
- stalin.su - partition editor: lists HDD/SSD/flash devices, edits GPT and MBR tables, recognises ext4, NTFS, UFS, FFS2 and Belarus (working: `src/stalin`)
- usa.su - C compiler written in USSR: C99 in, `.ru` bytecode out, with its own C library; builds pikchr, lemon and tpm (working: `src/usa`)
- sochi.su - Preferans for three by the Sochi rules, peer to peer over UDP with LAN discovery and chat (working: `src/sochi`)
- lenin.su - the brain center: refines a prompt, spins it through a centrifuge of AI models, measures the answers with your test and extracts the best (working: `src/lenin`)

Moscow, the amd64 kernel, booting in QEMU with russia.su as its shell:

![Moscow booting in QEMU and running print(_): "Hello, world!"](docs/images/moscow.gif)

The **ROUND stack** is the project's mainline: Ryazan (TLS) under Ukraine
(HTTP) under Novgorod (the forum), with Omsk (image thumbnails) making
Novgorod's 200x200 previews and Dubna (the PostgreSQL client) under
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

USSR inherits it's mother state traits and dramatic fate giant size uselessness dictatorship dissolution betrayed population
interpreter in suicide war against own assembler

USSR has a piece of ware for each decade of the computing since the PDP-11 vi fans of 70s to the AI vibe coders of 20s

It doesn't want to replace(you can't replace the lies that let a thousand days go)
Linux or Windows instead USSR refines enriches and complements with it while following it's own 3 tracks 


USSR is a neuroslopware it's created entirely by prompting an AI with beforehand refined prompt followed by enrichment results with the next AI "centrifuge"

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

## Lenin, the brain center of the USSR

Lenin is the neuroslop code refining and enrichment AI agent (working:
`src/lenin`, see [its README](src/lenin/README.md)).

USSR itself is made by prompting an AI with beforehand refined prompt and enrichment the results with the next
AI "centrifuge" by hand. Lenin is that process as a program. Practice what you
preach: if refined and enriched neuroslop is better than classical software then the
refining and enrichment has to be a tool and it has to show it.

```bash
ussr src/lenin/lenin.su --models models.conf --dir out --cycles 3 \
     --criteria speed --test "python test.py {file}" --ext .py "what you want"
```

It does the two operations of [neuroslop enrichment](research/README.md):

- **Refining** is a prompt operation. Prompt A, "make a text editor", and
  sub-task A, "it supports bold text", make prompt B, "make a text editor that
  supports bold text", until the prompt reflects what you really want. The
  sub-tasks are yours (`--task`), or found: with `--refine N` a model drafts code
  from the prompt, your test runs on it, and the model lists what the draft shows
  the prompt leaves out. That goes into the prompt, round after round, until a
  round finds nothing new.
- **Enrichment** chooses the better answer from several models used several
  times, with a test suite that says what is better, since writing that suite
  takes less intelligence than solving the problem. The current best goes on to
  the next model, or back to the same one, until nothing better comes that the
  tests can detect: `--method enrich`, with `--score` for a suite that counts
  how many of its checks pass, so that nearer beats further before anything
  passes in full. A hidden test, `--holdout`, keeps the models from fitting
  their code to the test they are shown, and when enrichment stalls on a
  failing best, Lenin goes back to refining the prompt.

**Enrichment is not voting.** It picks an answer on what a test showed, not on
how many models like it: one answer that passes beats ten that agree and fail.
Without a test Lenin can only let the models vote, and says that nothing it
picked that way was shown to work.
[research/refine_and_enrich.md](research/refine_and_enrich.md) sets the method
out at length.

```bash
ussr src/lenin/lenin.su --models models.conf --refine 3 --task "it supports bold text" \
     --method enrich --cycles 10 --score "python suite.py {file}" --ext .py "make a text editor"
```

You give it:

- the prompt, and any sub-tasks of it you know
- the centrifuge models, from different developers (the API keys stay in
  environment variables, the file only names them)
- the optimization criteria (speed, size)
- the working directory
- the generation cycles repeat count
- a test that says whether an answer works, or a suite that says how much of it
  works, and if you have one a classic solution to measure against

You get the best of what the models gave, by your test:

1. It refines the prompt so it precisely reflects things in reality: once, or
   in rounds of drafts and the sub-tasks they show are missing
2. It gives the refined prompt to the centrifuge models
3. They work on it for the given number of cycles, each cycle (or with
   `--method chain` each centrifuge, one after another) seeing the best
   answer so far and how it measured. `--method enrich` stops once a cycle
   finds nothing better
4. It runs every answer through your test and extracts the one that passes,
   scores highest on your suite and is best by your criteria

What it does and does not show:

- An answer that fails the test cannot win. That part is measured, not an
  opinion of a model. What refining finds missing is the opinion of a model:
  `tasks.md` lists it for you to read
- Best means best by the test you wrote. With a weak test the winner is whatever
  gets past it: given a canned answer `assert 2+3==5` to a task of printing a sum,
  and a test that only looked at the exit status, Lenin picks it for its size.
  Without a test the models only vote, and the report says nothing was run
- With a baseline, the report says whether the winner beat that classic solution
  on that one task
- Whether this adds up to better than classical software is not shown yet. So far
  Lenin has run against canned models only, to test Lenin itself. `src/lenin/examples/wordcount`
  is the first benchmark, waiting for real models and real keys

The project now has three main tracks, with the Moscow OS kernel underneath
(Lenin runs on a hosted `ussr` for now: on Moscow it has no TLS to reach a model):

1. The ROUND stack forum: Ryazan, Ukraine, Novgorod, Omsk and Dubna
2. The Kremlin GUI desktop and its apps, with the USSR shell
3. Lenin, the neuroslop code refining and enrichment AI agent

## The project is complete

With usa the second wave is done and USSR is complete. Its programs:

- ussr/ussr.exe - the USSR programming language and shell
- russia.su - bootstrapped USSR interpreter, written in USSR
- moscow.su - amd64 kernel: the NASM USSR runtime runs russia.su as its shell on a Belarus root (`sh src/moscow/build.sh`)
- lenin.su - the USSR brain center neuroslop refining and enrichment agent
- ukraine.su - HTTP server and web framework
- kiev.su - the USSR startup configuration file
- belarus.su - the USSR file system: per-file B+ tree block maps
- library (`src/libs`) - the USSR standard library: io, math, bytes, strings, vectors
- ural.su - USSR package manager
- petersburg - OpenGL GUI widgets, dialogs, events and layout
- Mbl.su - vi-like console text editor
- Mbltishi.su - terminal window of the desktop (on Moscow)
- tver.su - GUI two-panel file manager
- tula.su - GUI 2D CAD with DXF
- klin.su - GUI calculator: casual, scientific and programmer modes
- ryazan.su - TLS and HTTPS on Schannel or OpenSSL
- kremlin.su - GUI full-screen desktop on ~/kremlin
- novgorod.su - anonymous HTTPS image forum (ROUND stack)
- dubna.su - PostgreSQL client (ROUND stack)
- omsk.su - image thumbnails: PNG and baseline JPEG in pure USSR (ROUND stack)
- siberia.su - GUI spreadsheet
- novosibirsk.su - GUI document editor
- kgb.su - Knowledge Giga Base, fact-oriented Markdown knowledge base
- stalin.su - GPT/MBR partition editor
- sochi.su - three-player Preferans over UDP
- usa.su - C compiler to `.ru` bytecode
- install.su - the moscow OS installer: `install(_):` puts USSR on a disk of its own

This file is my personal devlog if you need user manual or more USSR design documents check the docs folder
If you want to add 3rd party USSR package project create a repo then add the box.uno then pack and upload it 
to the trusted mountain(ftp server) using ural 

I am far beyond postneuroslopware coder now I owned the WHOLE LOOP and found literally nothingness in the end on the other side
dreadful emptiness the machine asking for the next prompt the guy with dozens anime-avatar accounts is following me
AI is like a plastic cheap ubiquitous thing that wins the entire world 

Enough sentiments neuroslop is just a feature of the epoch I've got literally Ural mountains of it 

Take it or leave it(or push MOAR if you got it too)
If you are fine with the idea of refining and enrichment(or even researching) 
mass produced neuroslopware bring your model and get in too

Personally I believe the mass produced refined and enriched neuroslop is better than classical software
Cheaper faster less bugs per lines less requirement for programmer etc..
By classical software I mean a guy who sits 25-50 years in room in row improving some open source component
with end goal to get monopoly/dictatorship on it even it's technically stay open source
So in reality it needs 10.000 tons of highly enriched neuroslop not the code from eg. Linus Torvalds
Bill Gates or Richard Hipp

Now the whole thing asking me to become the kernel developer Will do anyway it's asking for it

P.S. СССР Средство Создания Совершенных Разработок
