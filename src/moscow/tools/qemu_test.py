#!/usr/bin/env python3
"""Boots build/moscow/ussr.img in QEMU and drives the Russia shell.

    python src/moscow/tools/qemu_test.py [--qemu PATH] [--transcript FILE]
                                         [--no-install]

QEMU runs headless with the first serial port on a local TCP socket.
The script waits for each "russia> " prompt (the PS1 Moscow sets), types
a line of USSR into src/russia/russia.su, the interpreter running as the
shell, and checks what it printed: plain USSR as on the hosted ussr, the
kernel's commands on its Belarus root file system (ls, cat, writefile,
run, ...) and the btree container.

Then it installs: a second boot with an empty qcow2 virtual hard disk
as /dev/hdb runs install(_):, answers its questions, and a third boot
starts from that disk alone and checks what the installer put there.
Prints PASS or FAIL per check and exits nonzero on any failure.
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
IMAGE = os.path.join(ROOT, "build", "moscow", "ussr.img")
PROMPT = b"russia> "

# command, then a regular expression the whole output must match. The
# shell is src/russia/russia.su, the USSR interpreter written in USSR;
# the answers are the ones it gives on the hosted ussr, except where the
# Moscow runtime says more (why an entry failed) or knows other files.
FAILED = r"USSR scheduler: process \d+ \(init\) failed\nUSSR VM: execution failed \(status -1\)"
CHECKS = [
    ('print(_): "hello, Moscow"', r"hello, Moscow"),
    ("foo(r): x [ set(r): {x * x} ]", r""),
    ("foo(z): 7", r""),
    ("print(_): z", r"49"),
    ("set(i): 0", r""),
    ("while(_): {i < 3} [ print(_): i set(i): {i + 1} ]", r"0\n1\n2"),
    ("set(t): 0", r""),
    ("set(k): 1", r""),
    ("while(_): {k <= 100} [ set(t): {t + k} set(k): {k + 1} ]", r""),
    ('if(_): {t = 5050} [ print(_): "sum ok" ] else(_): [ print(_): "bad" ]', r"sum ok"),
    ('concat(s): "Mos" "cow"', r""),
    ("print(_): s", r"Moscow"),
    ('struct(P): "x:number" "y:string"', r""),
    ('new(p): "P" 3 "q"', r""),
    ("print(_): p", r"P\{x=3,y=`q`\}"),
    ('getf(v): p "y"', r""),
    ("print(_): v", r"q"),
    ('vec(w): ""', r""),
    ("push(_): w 1", r""),
    ('push(_): w "two"', r""),
    ("print(_): w", r"\[1,`two`\]"),
    ("encode(e): w", r""),
    ("print(_): e", r"\[1,`two`\]"),
    ('decode(d): "[1,`a`,2.5]"', r""),
    ("print(_): d", r"\[1,`a`,2\.5\]"),
    ("print(_): {7 / 2}", r"3\.5"),
    ("print(_): {1 / 0}", r"USSR VM: division by zero \(ip \d+\)\n" + FAILED),
    ("nosuch(_): 1", r"USSR: unknown command 'nosuch' \(not in the Moscow runtime\)\n" + FAILED),
    # the btree container
    ("btree(bt):", r""),
    ('btput(_): bt "moscow" 1', r""),
    ('btput(_): bt "kiev" 2', r""),
    ("btput(_): bt 7 3", r""),
    ("print(_): bt", r"\[\[7,3\],\[`kiev`,2\],\[`moscow`,1\]\]"),
    ('btfloor(fl): bt "minsk"', r""),
    ("print(_): fl", r"kiev"),
    ("btlen(bn): bt", r""),
    ("print(_): bn", r"3"),
    # the kernel's commands, through russia.su's host calls
    ("uptime(_):", r"up \d+ s"),
    ("ls(_):", r"d +0  bin\nd +0  boot\nd +0  etc\nd +0  src"),
    ('ls(_): "/bin"', r"- +\d+  belarus\.ru\n- +\d+  install\.ru\n- +\d+  stalin\.ru"),
    ('cat(_): "/etc/hostname"', r"moscow-install"),
    ('writefile(_): "/home/notes.txt" "Belarus in Moscow"', r""),
    ('readfile(n): "/home/notes.txt"', r""),
    ("print(_): n", r"Belarus in Moscow"),
    ('file_exists(f): "/boot/moscow.ru"', r""),
    ("print(_): f", r"true"),
    ('file_exists(f): "/dev/hda"', r""),
    ("print(_): f", r"true"),
    ("df(_):", r"root: /dev/hda\nlabel ussr-install, uuid \w+\nsize 60 MiB, free [\d.]+ MiB; starts at sector 8192, tree root in block \d+"),
    ("fsck(_):", r"\d+ directories, \d+ files, \d+ bytes; \d+ tree nodes, \d+ leaves \(\d+ empty\)"),
    ('run(st): "stalin" "show" "/dev/hda"',
     r"Disk /dev/hda: 64 MiB, 131072 sectors of 512 bytes\nTable: MBR.*\n#.*\n1\* +8192 +131071 +122880 +60 MiB +belarus +belarus +ussr-install\nFree: 1\.\.8191 \(4 MiB\)"),
    ("print(_): st", r"0"),
    ("mem(m):", r"heap \d+ KiB, free \d+ KiB"),
    ("cpu(_):", r"\S.*"),
    ("uname(_):", r"USSR Moscow amd64"),
    ('getenv(g): "USER"', r""),
    ("print(_): g", r"comrade"),
    ('run(st): "nothing.ru"', r"USSR runtime: no file 'nothing\.ru' in the boot image"),
    ("print(_): st", r"127"),
]
# setenv changes the prompt; Ctrl-D ends the shell and the kernel halts

# the installer's questions and the answers given
ANSWERS = [
    (b"Install onto [/dev/hdb]: ", ""),
    (b"Choose [1]: ", ""),
    (b"or max [max]: ", ""),
    (b"File system label [ussr]: ", "comrade"),
    (b"Host name [moscow]: ", "kremlin"),
    (b"What to copy [1]: ", ""),
    (b"Type yes to go ahead [no]: ", "yes"),
]


def find_qemu(given):
    if given:
        return given
    for name in ("qemu-system-x86_64", "qemu-system-x86_64.exe"):
        path = shutil.which(name)
        if path:
            return path
    path = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
    return path if os.path.exists(path) else "qemu-system-x86_64"


def find_qemu_img(qemu):
    near = os.path.join(os.path.dirname(qemu), "qemu-img" + (".exe" if qemu.endswith(".exe") else ""))
    if os.path.exists(near):
        return near
    return shutil.which("qemu-img") or "qemu-img"


class Serial:
    def __init__(self, sock, log):
        self.sock = sock
        self.buf = b""
        self.log = log

    def read_until(self, marker, timeout=20.0):
        end = time.time() + timeout
        while marker not in self.buf:
            left = end - time.time()
            if left <= 0:
                raise TimeoutError("waiting for %r; got %r" % (marker, self.buf[-300:]))
            self.sock.settimeout(left)
            try:
                data = self.sock.recv(4096)
            except socket.timeout:
                continue
            if not data:
                raise EOFError("serial closed; got %r" % self.buf[-300:])
            self.log.append(data)
            self.buf += data
        i = self.buf.index(marker) + len(marker)
        out, self.buf = self.buf[:i], self.buf[i:]
        return out

    def command(self, line):
        self.sock.sendall(line.encode() + b"\r")
        echoed = self.read_until(b"\r\n")      # the echo of the line
        return echoed

    def output_to_prompt(self, timeout=20.0):
        out = self.read_until(PROMPT, timeout)
        return out[:-len(PROMPT)]


def clean(data):
    return data.decode("utf-8", "replace").replace("\r\n", "\n").rstrip("\n")


class Machine:
    """QEMU with the given disks (path, format), COM1 on a socket."""

    def __init__(self, qemu, disks, port, log):
        cmd = [qemu, "-display", "none", "-no-reboot", "-m", "2048",
               "-device", "isa-debug-exit,iobase=0xf4,iosize=4",
               "-serial", "tcp:127.0.0.1:%d,server=on,wait=on" % port]
        for i, (path, fmt) in enumerate(disks):
            cmd += ["-drive", "format=%s,index=%d,media=disk,file=%s" % (fmt, i, path)]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        sock = None
        for _ in range(100):
            try:
                sock = socket.create_connection(("127.0.0.1", port), timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        if sock is None:
            self.proc.kill()
            raise EOFError("cannot connect to QEMU's serial port")
        self.serial = Serial(sock, log)

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
        self.serial.sock.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu")
    ap.add_argument("--transcript")
    ap.add_argument("--port", type=int, default=45321)
    ap.add_argument("--no-install", action="store_true",
                    help="only the shell checks, no install onto a second disk")
    args = ap.parse_args()
    if not os.path.exists(IMAGE):
        print("no %s: run sh src/moscow/build.sh first" % IMAGE)
        return 2
    qemu = find_qemu(args.qemu)
    log = []
    failures = 0

    def check(label, ok, detail=""):
        nonlocal failures
        print("%s %s" % ("PASS" if ok else "FAIL", label))
        if not ok:
            failures += 1
            if detail:
                print(detail)

    # a copy, so the checks' writes leave the built disk as it was
    work = tempfile.mkdtemp(prefix="moscow_test_")
    disk = os.path.join(work, "ussr.img")
    shutil.copyfile(IMAGE, disk)

    # --- 1. the shell on the install disk
    m = Machine(qemu, [(disk, "raw")], args.port, log)
    serial = m.serial
    try:
        boot = clean(serial.output_to_prompt(timeout=60))
        check("boot reaches the russia prompt",
              "USSR runtime: NASM VM for amd64, running moscow.ru" in boot
              and "USSR Unified Shell Script REPL" in boot, boot)
        check("kernel lists the image",
              "image:   ussr_rt.bin moscow.ru russia.ru" in boot, boot)
        check("kernel mounts the Belarus root",
              "root:    /dev/hda from sector 8192, Belarus 'ussr-install'" in boot, boot)
        for line, want in CHECKS:
            serial.command(line)
            got = clean(serial.output_to_prompt())
            check(repr(line), re.fullmatch(want, got) is not None,
                  "  got: %r\n  want: %s" % (got, want))
        serial.command('setenv(_): "PS1" "moscow$ "')
        got = clean(serial.read_until(b"moscow$ ")[:-len(b"moscow$ ")])
        check("setenv PS1 changes the prompt", got == "", repr(got))
        serial.sock.sendall(bytes([4]))
        got = clean(serial.read_until(b"system halted", timeout=20))
        check("Ctrl-D ends the shell, the kernel halts",
              got.endswith("moscow: the shell has exited; system halted"), repr(got))
        try:
            code = m.proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            code = None
        # isa-debug-exit: writing 0 gives exit status 1
        check("QEMU exits through isa-debug-exit", code == 1, "exit status %r" % code)
    except (TimeoutError, EOFError) as e:
        check("serial conversation", False, str(e))
    finally:
        m.close()

    if not args.no_install:
        failures += install(qemu, disk, work, args.port + 1, log, check)

    if args.transcript:
        with open(args.transcript, "wb") as f:
            f.write(b"".join(log))
    shutil.rmtree(work, ignore_errors=True)
    print("%d failed" % failures)
    return 1 if failures else 0


def install(qemu, disk, work, port, log, check):
    """Installs onto an empty qcow2 disk, then boots it alone."""
    target = os.path.join(work, "target.qcow2")
    r = subprocess.run([find_qemu_img(qemu), "create", "-f", "qcow2", target, "96M"],
                       stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if r.returncode != 0:
        check("qemu-img makes a qcow2 disk", False, r.stderr.decode("utf-8", "replace"))
        return 0

    # --- 2. install(_): onto it
    m = Machine(qemu, [(disk, "raw"), (target, "qcow2")], port, log)
    serial = m.serial
    try:
        boot = clean(serial.output_to_prompt(timeout=60))
        check("the second disk is there", "disk:    /dev/hdb, 96 MiB, QEMU HARDDISK" in boot, boot)
        serial.command("install(_):")
        said = []
        for question, answer in ANSWERS:
            said.append(clean(serial.read_until(question, timeout=120)))
            serial.command(answer)
        said.append(clean(serial.output_to_prompt(timeout=900)))
        text = "\n".join(said)
        check("the installer lists the disks",
              "/dev/hda  64 MiB  QEMU HARDDISK  (the install disk)" in text
              and "/dev/hdb  96 MiB  QEMU HARDDISK" in text, text)
        check("stalin makes the partition",
              re.search(r"1\* +8192 +196607 +188416 +92 MiB +belarus", text) is not None, text)
        check("it copies everything",
              re.search(r"\d+ files and directories copied; checking:", text) is not None
              and "  /src/ussr/src/belarus/" in text, text)
        check("the new file system checks clean",
              re.search(r"\n\d+ directories, \d+ files, \d+ bytes; \d+ tree nodes, \d+ leaves \(\d+ empty\)\n", text)
              is not None, text)
        check("the new disk is bootable",
              "Done: /dev/hdb boots USSR Moscow now." in text, text)
        serial.sock.sendall(bytes([4]))
        serial.read_until(b"system halted", timeout=20)
    except (TimeoutError, EOFError) as e:
        check("installer conversation", False, str(e))
    finally:
        m.close()

    # --- 3. the installed disk, alone
    m = Machine(qemu, [(target, "qcow2")], port + 1, log)
    serial = m.serial
    try:
        boot = clean(serial.output_to_prompt(timeout=60))
        check("the installed disk boots",
              "root:    /dev/hda from sector 8192, Belarus 'comrade'" in boot
              and "USSR Moscow on kremlin, installed from ussr-install." in boot, boot)
        for line, want in [
            ('getenv(h): "HOSTNAME"', r""),
            ("print(_): h", r"kremlin"),
            ('cat(_): "/etc/hostname"', r"kremlin"),
            ('ls(_): "/src/ussr/src/belarus"', r"- +\d+  README\.md\n- +\d+  belarus\.su"),
            ('readfile(c): "/src/ussr/src/belarus/belarus.su"', r""),
            ("len(cl): c", r""),
            ("print(_): {cl > 10000}", r"true"),
            ("fsck(_):", r"\d+ directories, \d+ files, \d+ bytes; \d+ tree nodes, \d+ leaves \(0 empty\)"),
        ]:
            serial.command(line)
            got = clean(serial.output_to_prompt())
            check("installed: " + repr(line), re.fullmatch(want, got) is not None,
                  "  got: %r\n  want: %s" % (got, want))
        serial.sock.sendall(bytes([4]))
        serial.read_until(b"system halted", timeout=20)
    except (TimeoutError, EOFError) as e:
        check("installed disk conversation", False, str(e))
    finally:
        m.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
