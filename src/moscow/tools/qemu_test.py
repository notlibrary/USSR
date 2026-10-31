#!/usr/bin/env python3
"""Boots build/moscow/moscow.img in QEMU and drives the Russia shell.

    python src/moscow/tools/qemu_test.py [--qemu PATH] [--transcript FILE]

QEMU runs headless with the first serial port on a local TCP socket.
The script waits for each "russia> " prompt (the PS1 Moscow sets), types
a line of USSR into src/russia/russia.su, the interpreter running as the
shell, and checks what it printed: plain USSR as on the hosted ussr, and
the kernel's own commands (uptime, ls, mem, ...).
Prints PASS or FAIL per check and exits nonzero on any failure.
"""
import argparse
import os
import re
import shutil
import socket
import subprocess
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
IMAGE = os.path.join(ROOT, "build", "moscow", "moscow.img")
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
    # the kernel's commands, through russia.su's host calls
    ("uptime(_):", r"up \d+ s"),
    ("ls(_):", r"ussr_rt\.bin\nmoscow\.ru\nrussia\.ru"),
    ("mem(m):", r"heap \d+ KiB, free \d+ KiB"),
    ("print(_): m", r"\[\d+,\d+\]"),
    ("cpu(_):", r"\S.*"),
    ("uname(_):", r"USSR Moscow amd64"),
    ('file_exists(f): "moscow.ru"', r""),
    ("print(_): f", r"true"),
    ('getenv(g): "USER"', r""),
    ("print(_): g", r"comrade"),
    ('run(st): "nothing.ru"', r"USSR runtime: no file 'nothing\.ru' in the boot image"),
    ("print(_): st", r"127"),
]
# setenv changes the prompt; Ctrl-D ends the shell and the kernel halts


def find_qemu(given):
    if given:
        return given
    for name in ("qemu-system-x86_64", "qemu-system-x86_64.exe"):
        path = shutil.which(name)
        if path:
            return path
    path = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
    return path if os.path.exists(path) else "qemu-system-x86_64"


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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu")
    ap.add_argument("--transcript")
    ap.add_argument("--port", type=int, default=45321)
    args = ap.parse_args()
    if not os.path.exists(IMAGE):
        print("no %s: run sh src/moscow/build.sh first" % IMAGE)
        return 2

    qemu = subprocess.Popen(
        [find_qemu(args.qemu), "-drive", "format=raw,file=" + IMAGE,
         "-display", "none", "-no-reboot", "-m", "2048",
         "-device", "isa-debug-exit,iobase=0xf4,iosize=4",
         "-serial", "tcp:127.0.0.1:%d,server=on,wait=on" % args.port],
        stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    sock = None
    for _ in range(100):
        try:
            sock = socket.create_connection(("127.0.0.1", args.port), timeout=1)
            break
        except OSError:
            time.sleep(0.1)
    if sock is None:
        qemu.kill()
        print("FAIL cannot connect to QEMU's serial port")
        return 1

    log = []
    serial = Serial(sock, log)
    failures = 0

    def check(label, ok, detail=""):
        nonlocal failures
        print("%s %s" % ("PASS" if ok else "FAIL", label))
        if not ok:
            failures += 1
            if detail:
                print(detail)

    try:
        boot = clean(serial.output_to_prompt(timeout=60))
        check("boot reaches the russia prompt",
              "USSR runtime: NASM VM for amd64, running moscow.ru" in boot
              and "USSR Unified Shell Script REPL" in boot, boot)
        check("kernel lists the image",
              "image:   ussr_rt.bin moscow.ru russia.ru" in boot, boot)
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
        # the nested kernel halts the machine when its shell exits, as
        # the outer one would
        try:
            code = qemu.wait(timeout=20)
        except subprocess.TimeoutExpired:
            code = None
        # isa-debug-exit: writing 0 gives exit status 1
        check("QEMU exits through isa-debug-exit", code == 1, "exit status %r" % code)
    except (TimeoutError, EOFError) as e:
        check("serial conversation", False, str(e))
    finally:
        if qemu.poll() is None:
            qemu.kill()
        sock.close()
        if args.transcript:
            with open(args.transcript, "wb") as f:
                f.write(b"".join(log))

    print("%d failed" % failures)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
