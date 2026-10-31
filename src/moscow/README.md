# Moscow

Moscow is the USSR kernel for amd64 PCs. A NASM boot loader starts a
NASM USSR runtime on bare metal, the runtime runs the kernel `moscow.ru`
(compiled from `moscow.su`), and the kernel runs the shell `russia.ru`
(compiled from `russia.su`). All four go on one boot disk:

| Part | Source | On the disk |
|------|--------|-------------|
| 1. boot loader | `arch/amd64/boot.nasm` | sectors 0..15 |
| 2. USSR runtime | `arch/amd64/ussr_rt.nasm` | `ussr_rt.bin` |
| 3. kernel | `moscow.su` | `moscow.ru` |
| 3. shell | `russia.su` | `russia.ru` |

## Build and run

Needs NASM, QEMU and a built `ussr` (it compiles the two `.su` files):

    sh src/moscow/build.sh                  # -> build/moscow/moscow.img
    sh src/moscow/run.sh                    # window + serial on the terminal
    python src/moscow/tools/qemu_test.py    # headless test over serial

`moscow.img` is a raw disk image; `dd` it to a USB stick to boot a real
PC with a BIOS (or CSM) whose disk answers on the legacy ATA ports (IDE,
or SATA in IDE mode); USB and AHCI drivers are not there yet.

## Boot

- **Stage 1** (the MBR, 512 bytes) reads stage 2 with the BIOS extended
  disk read and jumps to it.
- **Stage 2** uses the BIOS only for what only it knows: A20 and the
  e820 memory map. It builds identity page tables for the first 4 GiB
  (2 MiB pages) and goes from real mode straight to long mode. From
  there on everything is 64-bit: an ATA PIO driver (primary master,
  LBA28) reads the file table (`MOSCOWFS`, sector 16) and every file it
  lists to memory from 1 MiB up, with no 640 KiB window, and stage 2
  jumps to the runtime (always the first file) with the boot information
  block in `rdi`. The heap starts at the next 2 MiB after the files.
- `arch/amd64/layout.inc` holds the sector and memory layout;
  `arch/amd64/image.nasm` assembles the disk with `incbin`.

Progress messages go to the screen and COM1, so a headless boot shows
where it stopped.

## The runtime

`ussr_rt.nasm` is the USSR VM in assembly. It sets up the console (VGA
text mode and COM1 at 115200), the IDT (CPU exceptions print and stop),
the PIT at 100 Hz and a heap from 2 MiB to the end of RAM, then loads
`moscow.ru` and runs it.

It reads `.ru` files as `ussr -c` writes them (format 3, VM ABI 3,
checked with the CRC) and runs them as `docs/USSR_VM_SPEC.md` describes:
32 registers, frames with their own locals, `init(ret): arg_cnt arg_vec`
as the entry point, `name!`/`name?` hash entries. Names are interned at
load time, so variables and builtins are found by index. Values are
null, 64-bit integers, reals (SSE2, printed like `%g`), strings, booleans
and vectors; structs are not there yet. Not supported: `EXTERNAL`,
`EVAL`, `SCAN`, `DECODE_UNO`, `GET`, `CHAIN`, `FILE`, `CD`, `TEMPLATE`
(a program using one stops with a message). `time` reads the RTC.

Builtins: `len byte slice chr vec push pop at typeof`, with the hosted
ussr's error messages. Library code such as `src/libs/strings.su` is
compiled into the `.ru`, so `str_from_int`, `str_trim` and the rest work.

Kernel services:

| Command | Does |
|---------|------|
| `k_getline(line):` | a console line (keyboard or serial), echoed, Backspace erases |
| `k_prompt(_): text` | text without a line end |
| `k_clear(_):` | clears the screen |
| `k_color(_): fg bg` | VGA colours 0..15 |
| `k_ticks(n):` | timer ticks since boot, 100 a second |
| `k_mem(v):` | `[total free]` heap bytes |
| `k_cpu(s):` | CPUID vendor and brand |
| `k_modules(v):` | file names in the boot image |
| `k_exec(status): name` | runs a `.ru` of the image to its end; 127 when missing |
| `k_reboot(_):`, `k_halt(_):` | reset; stop (QEMU exits with isa-debug-exit) |

Memory is a bump heap with marks: `k_exec` releases everything the
program allocated when it ends, so a shell that exits returns its memory.
There is no collector inside one program yet.

## The kernel and the shell

`moscow.su` prints the CPU, the heap and the image's files, then runs
`russia.ru` and runs it again if it fails; when the shell exits normally
the machine halts.

`russia.su` (the Moscow shell; not the self-hosted interpreter in
`src/russia`, though it does the same job at a smaller scale) reads
command lines: `help`, `echo` (with `$NAME` variables), `set`, `env`,
`calc` (integer `+ - * / % ( )`), `clear`, `color`, `mem`, `uptime`,
`cpu`, `ls`, `run FILE`, `history`, `uname`, `exit`, `reboot`, `halt`.

## Tools

- `tools/rudump.py program.ru` lists a `.ru` file's instructions;
  `--summary` lists the opcodes and commands it uses, which is what the
  runtime has to provide.
- `tools/qemu_test.py` boots the image with COM1 on a TCP socket, types
  commands and checks each answer (the same answers the shell gives on
  the hosted ussr), including a nested `run moscow.ru`, and checks that
  the final `exit` halts QEMU.
