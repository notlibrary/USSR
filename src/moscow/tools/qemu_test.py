#!/usr/bin/env python3
"""Boots build/moscow/moscow.img in QEMU and drives the Russia shell.

    python src/moscow/tools/qemu_test.py [--qemu PATH] [--transcript FILE]

QEMU runs headless with the first serial port on a local TCP socket.
The script waits for each "russia> " prompt, types a command, and checks
what the command printed. The expectations match what the shell prints
on the hosted ussr (the same russia.su with the k_* services stubbed).
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

# command, then a regular expression the whole output must match
CHECKS = [
    ("echo   hello   world ", r"hello world"),
    ("set NAME Ivan", r""),
    ("echo hi $NAME and $SHELL$OS!", r"hi Ivan and russiamoscow!"),
    ("set NAME Petr", r""),
    ("env", r"SHELL=russia\nOS=moscow\nNAME=Petr"),
    ("calc 1 + 2 * 3", r"7"),
    ("calc (1+2)*3 - -4", r"13"),
    ("calc 7 / 2", r"3"),
    ("calc -7 / 2", r"-3"),
    ("calc 7 % 3", r"1"),
    ("calc 1 / 0", r"calc: division by zero"),
    ("calc 2 +", r"calc: unexpected end"),
    ("calc 2 3", r"calc: unexpected 3"),
    ("calc 123456789 * 1000", r"123456789000"),
    ("mem", r"heap \d+ KiB, free \d+ KiB"),
    ("uptime", r"up \d+ s"),
    ("cpu", r"\S.*"),
    ("ls", r"ussr_rt\.bin\nmoscow\.ru\nrussia\.ru"),
    ("uname", r"USSR Moscow amd64"),
    ("frob", r"frob: command not found \(try help\)"),
    ("run nothing.ru", r"USSR runtime: no file 'nothing\.ru' in the boot image\nnothing\.ru exited with 127"),
    ("run moscow.ru",
     r"USSR Moscow kernel for amd64\n(?:.*\n)*moscow: starting russia\.ru\n"
     r"Russia shell\. Type help for the commands\."),
]
# nested: run moscow.ru starts a second kernel and shell; its exit halts
# the machine
NESTED = [
    ("echo inside", r"inside"),
    ("help", r"Russia, the Moscow shell\. Commands:\n(?:  .*\n)*  reboot / halt        reset or stop the machine"),
    ("history", r"  1  echo inside\n  2  help\n  3  history"),
    ("exit", r"bye\nmoscow: the shell has exited; system halted"),
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
         "-display", "none", "-no-reboot", "-m", "128",
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
              and "Russia shell. Type help for the commands." in boot, boot)
        check("kernel lists the image",
              "image:   ussr_rt.bin moscow.ru russia.ru" in boot, boot)
        for line, want in CHECKS + NESTED:
            serial.command(line)
            if line == "exit":
                got = clean(serial.read_until(b"system halted", timeout=20))
            else:
                got = clean(serial.output_to_prompt())
            check(repr(line), re.fullmatch(want, got) is not None,
                  "  got: %r\n  want: %s" % (got, want))
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
