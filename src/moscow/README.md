# Moscow

Moscow is the USSR kernel for amd64 PCs. A NASM boot loader starts a
NASM USSR runtime on bare metal, the runtime runs the kernel `moscow.ru`
(compiled from `moscow.su`), and the kernel runs the shell `russia.ru`:
`src/russia/russia.su`, the USSR interpreter written in USSR, whose REPL
is the shell. All four go on one boot disk:

| Part | Source | On the disk |
|------|--------|-------------|
| 1. boot loader | `arch/amd64/boot.nasm` | sectors 0..15 |
| 2. USSR runtime | `arch/amd64/ussr_rt.nasm` | `ussr_rt.bin` |
| 3. kernel | `moscow.su` | `moscow.ru` |
| 3. shell | `../russia/russia.su` | `russia.ru` |

## Build and run

Needs NASM, QEMU and a built `ussr` (it compiles the two `.su` files):

    sh src/moscow/build.sh                  # -> build/moscow/moscow.img
    sh src/moscow/run.sh                    # window + serial on the terminal
    python src/moscow/tools/qemu_test.py    # headless test over serial

`moscow.img` is a raw disk image; `dd` it to a USB stick to boot a real
PC with a BIOS (or CSM) whose disk answers on the legacy ATA ports (IDE,
or SATA in IDE mode); USB and AHCI drivers are not there yet.

## A session

    USSR Unified Shell Script REPL
    USSR v0.4
    Enter a command list or press Ctrl-D to exit.

    russia> foo(r): x [ set(r): {x * x} ]
    russia> foo(z): 7
    russia> print(_): z
    49
    russia> struct(P): "x:number" "y:string"
    russia> new(p): "P" 3 "q"
    russia> print(_): p
    P{x=3,y=`q`}
    russia> uptime(_):
    up 12 s
    russia> ls(_):
    ussr_rt.bin
    moscow.ru
    russia.ru
    russia> help(_):

Everything typed is USSR, run by russia.su's tree walker. The commands
russia.su does not know itself go to its host the way they go to ussr
when it runs on Windows or Linux (`ex_ext_b`: an `eval` of a one-line
call); on Moscow the host is the runtime, so they are kernel commands:

| Command | Prints and returns |
|---------|--------------------|
| `uptime(s):` | seconds since boot |
| `ls(v):` | the files of the boot image |
| `mem(v):` | `[heap bytes, free bytes]` |
| `cpu(s):` | CPUID vendor and brand |
| `uname(s):`, `pwd(s):` | `USSR Moscow amd64`, `/` |
| `clear(_):`, `color(_): fg bg` | the screen, VGA colours 0..15 |
| `run(status): "f.ru"` | runs a program of the image; 127 when missing |
| `sleep(_): ms` | waits |
| `getenv`, `setenv`, `unsetenv` | the environment (`PS1` is the prompt) |
| `file_exists(b): f`, `readfile(s): f` | files of the image |
| `reboot(_):`, `halt(_):` | reset; stop |
| `help(_):` | this list |

Ctrl-D on an empty line ends the shell, and Moscow halts the machine
(QEMU exits when started with `isa-debug-exit`).

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
  block in `rdi`.
- `arch/amd64/layout.inc` holds the sector and memory layout;
  `arch/amd64/image.nasm` assembles the disk with `incbin`.

Progress messages go to the screen and COM1, so a headless boot shows
where it stopped.

## The runtime

`ussr_rt.nasm` is the USSR VM in assembly. It loads its own GDT and IDT
(CPU exceptions print and stop), sets up the console (VGA text mode and
COM1 at 115200, the PS/2 keyboard), the PIT at 100 Hz and the heap, then
loads `moscow.ru` and runs it.

Memory: the heap is the largest usable RAM region of the BIOS map above
the loaded files. When RAM goes past the boot loader's 4 GiB identity
map, the runtime maps the rest (2 MiB pages, up to 64 GiB) with tables
taken from the heap. russia.ru needs a few hundred MiB for a long
session; the scripts give QEMU 2 GiB.

It reads `.ru` files as `ussr -c` writes them (format 3, VM ABI 3,
checked with the CRC) and runs them as `docs/USSR_VM_SPEC.md` describes:
32 registers, frames with their own locals, `init(ret): arg_cnt arg_vec`
as the entry point, `name!`/`name?` hash entries. Names are interned at
load time, so variables and builtins are found by index; the call-site
table gives builtins their return name (`struct(Name):`). Values are
null, 64-bit integers, reals (SSE2, printed like `%g`), strings,
booleans, vectors and structs. `EVAL` runs what russia.su hands its host
(lines of builtin calls with variable, string, number or literal
arguments); `TEMPLATE` prints; `try` runs a function in a VM of its own
and reports whether it failed. Not supported: `EXTERNAL`, `SCAN`,
`DECODE_UNO`, `GET`, `CHAIN`, `FILE`, `CD` (a program using one stops
with a message). `time` reads the RTC.

Builtins with the hosted ussr's meaning: `len byte slice chr vec push
pop at typeof struct new getf setf encode decode try write_stderr fail
text_bytes bytes_text crc32 getenv setenv unsetenv getcwd terminal_getch
terminal_raw terminal_sane terminal_key_ready terminal_size sleep
file_exists readfile` (`gc`, `alias`, `keybind` answer null). Library
code such as `src/libs/strings.su` is compiled into the `.ru`.
`terminal_getch` hands out a line at a time: the line is read with echo
and Backspace, as a cooked terminal does, so russia.su reads it as from
a pipe.

The kernel's own services, used by `moscow.su`:

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
| `k_reboot(_):`, `k_halt(_):` | reset; stop |

The heap is a bump heap with marks: `k_exec` (and `run`) release
everything the program allocated, and the struct types it declared,
when it ends. There is no collector inside one program yet.

## Tools

- `tools/rudump.py program.ru` lists a `.ru` file's instructions;
  `--summary` lists the opcodes and commands it uses, which is what the
  runtime has to provide.
- `tools/qemu_test.py` boots the image with COM1 on a TCP socket, types
  USSR into the russia.su REPL and checks each answer (as the hosted
  ussr gives them, plus the kernel commands), changes the prompt with
  `setenv`, and checks that Ctrl-D halts QEMU.
