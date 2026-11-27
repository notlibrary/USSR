# China

China is one USSR package containing native Moscow command-line programs.
Each command is built into a separate `.ru` under `/bin` on the live ISO
and on the installed system. From Rush, use `china():` to list the commands and
`NAME(): [arguments ...]` to launch one.

    head(_): "/etc/motd" 3
    grep(): "^screen=" "/etc/gui.conf"
    mkdir(): "/home/notes"
    cd(): "/home/notes"
    /bin/pwd.ru():

China uses its own argument conventions. File names are relative to the
current Belarus directory unless they start with `/`.

| Command | Operands and behavior |
| --- | --- |
| `head`, `tail` | `FILE [LINES]`, default 10 |
| `chmod`, `chown` | `chmod [-R] MODE PATH` changes octal permissions. Use `chmod(): "-R" 755 "/path"` in Rush to include all descendants. `chown OWNER PATH` changes the owner. |
| `stat` | `PATH`, print mode, owner, and path |
| `cat` | `FILE ...`, display text in order |
| `more`, `less` | `FILE`, Space or Enter to advance, q to quit, b to go back in less |
| `grep` | `REGEX FILE`, statuses 0 for a match, 1 for no match, 2 for an error |
| `ps` | List Moscow tasks |
| `kill` | `TASK_ID`, stop a Moscow task |
| `ls`, `tree` | `[PATH]`, default current directory |
| `df` | Report Belarus root capacity |
| `du` | `[PATH]`, total logical file bytes under the path |
| `mount` | Show the active Belarus root and its start sector |
| `dmesg` | Show the runtime and kernel boot log captured before Rush starts, up to 4 KiB |
| `mv`, `cp` | `FROM TO`, accept a directory as the destination, refuse to replace an existing entry. `cp` copies individual files, including binary files. |
| `rm` | `FILE ...`, remove files |
| `mkdir`, `rmdir` | `DIRECTORY`, create a directory and its parents or remove an empty directory |
| `cd`, `pwd` | `cd [DIRECTORY]` changes Rush's current directory, defaulting to HOME. `pwd` prints it. |
| `who`, `whoami` | Display the kernel console identity |
| `reguser` | `USERNAME [ROLE]`, create an account from a master-admin session. The default role is `user`. Creates a private home in `/home/USERNAME`. |
| `passwd` | Reports that password changes are unavailable |
| `date` | UTC clock as ISO 8601, or seconds since the epoch with `epoch` |
| `uname`, `uptime` | Show system identity or elapsed time since boot |
| `echo` | Print arguments separated by spaces |
| `touch` | `FILE`, create an empty file if absent and preserve an existing file |
| `wc` | `FILE`, count text lines (including a final unterminated line) and bytes |

The text commands take file operands and expect text without NUL bytes.
The live ISO mounts its Belarus root in RAM, so edits last until reboot.

Run the live ISO checks after building with `make -f moscow.mk iso`:

    python src/moscow/tools/china_test.py --qemu qemu-system-x86_64
