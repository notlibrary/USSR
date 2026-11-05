#!/usr/bin/env python3
"""Boots build/moscow/ussr.img in QEMU and drives the Russia shell.

    python src/moscow/tools/qemu_test.py [--qemu PATH] [--transcript FILE]
                                         [--accel whpx|kvm] [--no-mbl]
                                         [--no-novgorod] [--no-install]

QEMU runs headless with the first serial port on a local TCP socket.
The script waits for each "russia> " prompt (the PS1 Moscow sets), types
a line of USSR into src/russia/russia.su, the interpreter running as the
shell, and checks what it printed: plain USSR as on the hosted ussr, the
kernel's commands on its Belarus root file system (ls, cat, writefile,
run, ...) and the btree container.

Then the Mbl editor (src/Mbl/Mbl.su, run from its source) writes a note,
checked on the serial line and in the VGA text memory (read through the
QEMU monitor); halt(_): stops the machine and the next boot finds the
note on the disk and opens it again (--no-mbl skips this).

Then the network: Novgorod, the forum (src/novgorod/novgorod.su), runs
from its source over Moscow's TCP/IP stack with a port of this machine
forwarded to it, its posts in a Dubna KV store (--kvdb); the script reads
its pages and posts to it, and after the next boot Novgorod serves the
post again from the store (--no-novgorod skips this).

Then TLS: Moscow, a client through Ryazan and BearSSL (/lib/tls.so,
loaded by the kernel's ELF loader), fetches a page from an HTTPS server
on this machine, and Novgorod serves HTTPS, its certificate checked by
this script (--no-tls skips this).

Then it installs: a second boot with an empty qcow2 virtual hard disk
as /dev/hdb runs install(_):, answers its questions, and a third boot
starts from that disk alone and checks what the installer put there.
Prints PASS or FAIL per check and exits nonzero on any failure.
"""
import argparse
import functools
import os
import re
import shutil
import socket
import subprocess
import sys
import ssl
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, HTTPServer, SimpleHTTPRequestHandler, ThreadingHTTPServer
import urllib.error
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
IMAGE = os.path.join(ROOT, "build", "moscow", "ussr.img")
PROMPT = b"russia> "
ACCEL = None            # --accel: QEMU's -accel (whpx, kvm), else TCG

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
    # the shell keeps its history in $HOME/.ussr_history, HOME being /
    ("ls(_):", r"- +\d+  \.ussr_history\nd +\d+  bin\nd +\d+  boot\nd +\d+  etc\nd +\d+  lib\nd +\d+  src"),
    ('ls(_): "/bin"', r"- +\d+  belarus\.ru\n- +\d+  install\.ru\n(- +\d+  moscow_native\n)?- +\d+  stalin\.ru(\n- +\d+  ural)?(\n- +\d+  ussr)?"),
    ('cat(_): "/etc/hostname"', r"moscow-install"),
    ('writefile(_): "/home/notes.txt" "Belarus in Moscow"', r""),
    ('readfile(n): "/home/notes.txt"', r""),
    ("print(_): n", r"Belarus in Moscow"),
    # Up recalls the last line from the history: the shell reads keys raw
    ("\x1b[A", r"Belarus in Moscow"),
    # files by handle
    ('open_file(h): "/home/log.txt" "w"', r""),
    ('write_file(_): h "one\\ntwo\\r\\n"', r""),
    ('write_file(_): h "three"', r""),
    ("close_file(_): h", r""),
    ('open_file(h): "/home/log.txt" "a"', r""),
    ('write_file(_): h "!"', r""),
    ("close_file(_): h", r""),
    ('open_file(h): "/home/log.txt" "rb"', r""),
    ("read_line(a): h", r""),
    ("read_line(b): h", r""),
    ("read_line(c): h", r""),
    ("read_line(d): h", r""),
    ("close_file(_): h", r""),
    ("print(_): a", r"one"),
    ("print(_): b", r"two"),
    ("print(_): c", r"three!"),
    ("typeof(t): d", r""),
    ("print(_): t", r"null"),
    ('open_file(h): "/home/none.txt" "r"', r""),
    ("print(_): h", r"-1"),
    ('file_exists(f): "/boot/moscow.ru"', r""),
    ("print(_): f", r"true"),
    ('file_exists(f): "/dev/hda"', r""),
    ("print(_): f", r"true"),
    ("df(_):", r"root: /dev/hda\nlabel ussr-install, uuid \w+\nsize 60 MiB, free [\d.]+ MiB; starts at sector 8192, root directory in inode \d+"),
    ("fsck(_):", r"\d+ directories, \d+ files, \d+ bytes; \d+ inodes, \d+ block map nodes, map depth up to 2"),
    # russia.ru is 216 blocks: its block map has outgrown the inode
    ('run(st): "belarus" "fsck" "/dev/hda"', r"\d+ directories, \d+ files, \d+ bytes; \d+ inodes, \d+ block map nodes, map depth up to \d+"),
    ('run(st): "stalin" "show" "/dev/hda"',
     r"Disk /dev/hda: 64 MiB, 131072 sectors of 512 bytes\nTable: MBR.*\n#.*\n1\* +8192 +131071 +122880 +60 MiB +belarus +belarus +ussr-install\nFree: 1\.\.8191 \(4 MiB\)"),
    ("print(_): st", r"0"),
    # the network
    ("net(_):", r"e1000 52:54:00:12:34:56, 10\.0\.2\.15/24 \(DHCP\), gateway 10\.0\.2\.2"),
    ('ping(n): "10.0.2.2" 2', r"reply from 10\.0\.2\.2: seq 1, \d+ ms\nreply from 10\.0\.2\.2: seq 2, \d+ ms\n2 sent, 2 answered"),
    ("print(_): n", r"2"),
    # the DHCP client: the lease, renewed, given back and taken again
    ("dhcp(_):", r"10\.0\.2\.15 from 10\.0\.2\.2, lease 86400s, renews in 43\d\d\ds, ends in 86\d\d\ds"),
    ('dhcp(d): "renew"', r"ack\n10\.0\.2\.15 from 10\.0\.2\.2, lease 86400s, renews in 43200s, ends in 86400s"),
    ("print(_): d", r"\[`bound`,`10\.0\.2\.15`,`10\.0\.2\.2`,86400,86400\]"),
    ('dhcp(_): "release"', r"no lease: the address was given back"),
    ("net(_):", r"e1000 52:54:00:12:34:56, 0\.0\.0\.0/24 \(released\), gateway 10\.0\.2\.2"),
    ('dhcp(_): "release"', r"no lease to give back\nno lease: the address was given back"),
    ('dhcp(_): "renew"', r"new\n10\.0\.2\.15 from 10\.0\.2\.2, lease 86400s, renews in 43200s, ends in 86400s"),
    ('dhcp(_): "sideways"', r"dhcp: renew or release, not sideways\n10\.0\.2\.15 from (.|\n)*"),
    ('ping(n): "10.0.2.2" 1', r"reply from 10\.0\.2\.2: seq 1, \d+ ms\n1 sent, 1 answered"),
    # Murmansk, the firewall
    ("fw(_):", r"policy in deny, out allow\n1\. allow in icmp   \(0 hits\)\n2\. allow in tcp 8080   \(0 hits\)\n3\. allow in tcp 8443   \(0 hits\)\n\d+ packets dropped(; the last:(\n  .*)+)?"),
    ('fw(_): "deny out icmp to 10.0.2.2"', r"(.|\n)*4\. deny out icmp to 10\.0\.2\.2   \(0 hits\)(.|\n)*"),
    ('ping(n): "10.0.2.2" 1', r"ping 10\.0\.2\.2: Murmansk denies it \(fw\(_\): lists the rules\)"),
    ('fw(_): "delete 4"', r"(.|\n)*3\. allow in tcp 8443(.|\n)*"),
    ('fw(_): "allow sideways"', r"fw: in or out, not sideways\n(.|\n)*"),
    ('ping(n): "10.0.2.2" 1', r"reply from 10\.0\.2\.2: seq 1, \d+ ms\n1 sent, 1 answered"),
    # C libraries: /lib/ctest.so through the runtime's ELF loader
    ('run(st): "/src/ussr/tests/cffi_moscow_test.su"', r"(PASS [^\n]*\n){16}PASS [^\n]*"),
    # a program's C code runs in ring 3: its own memory is fine, the
    # kernel's page tables and cli are not; the kernel goes on
    ('run(st): "/src/ussr/tests/moscow_ring3.su" "own"', r"ring3: moscow_ticks answers\nring3: its own memory is its own"),
    ("print(_): st", r"0"),
    ('run(st): "/src/ussr/tests/moscow_ring3.su" "poke"',
     r"ring3: moscow_ticks answers\nring3: its own memory is its own\nring3: writing the kernel's page tables\n"
     r"USSR runtime: page fault at 0x0+1000\n"
     r"USSR runtime: C code in ring 3 stopped: exception 14, rip 0x[0-9a-f]+, address 0x0+1000\n"
     r"USSR: c_call: the C code broke the rules of ring 3 and was stopped\n"
     r"USSR VM: execution failed \(ip \d+\)"),
    ("print(_): st", r"1"),
    ('run(st): "/src/ussr/tests/moscow_ring3.su" "cli"',
     r"ring3: moscow_ticks answers\nring3: its own memory is its own\nring3: interrupts off\n"
     r"USSR runtime: C code in ring 3 stopped: exception 13, rip 0x[0-9a-f]+, address 0x[0-9a-f]+\n"
     r"USSR: c_call: the C code broke the rules of ring 3 and was stopped\n"
     r"USSR VM: execution failed \(ip \d+\)"),
    ("print(_): st", r"1"),
    ('ls(_): "/etc"', r"(.*\n)*-  +\d+  murmansk\.rules(\nd +\d+  ssl)?"),
    ('localtime(t): 1793535166', r""),
    ("print(_): t", r"\[2026,11,1,12,12,46,0,304\]"),
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


# sendkey names of the characters the GUI checks type
SENDKEY = {" ": "spc", "(": "shift-9", ")": "shift-0", "_": "shift-minus",
           ":": "shift-semicolon", '"': "shift-apostrophe", ".": "dot",
           "/": "slash", "\n": "ret"}


def pixel(shot, x, y):
    w, h, rgb = shot
    i = (y * w + x) * 3
    return tuple(rgb[i:i + 3])


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

    def __init__(self, qemu, disks, port, log, monitor=None, forward=()):
        cmd = [qemu, "-display", "none", "-no-reboot", "-m", "2048",
               "-device", "isa-debug-exit,iobase=0xf4,iosize=4",
               "-serial", "tcp:127.0.0.1:%d,server=on,wait=on" % port]
        if monitor:
            cmd += ["-monitor", "tcp:127.0.0.1:%d,server=on,wait=off" % monitor]
        # an e1000 on QEMU's user network (10.0.2.0/24), with host ports
        # forwarded to the guest's
        netdev = "user,id=n0" + "".join(",hostfwd=tcp:127.0.0.1:%d-:%d" % f for f in forward)
        cmd += ["-netdev", netdev, "-device", "e1000,netdev=n0"]
        if ACCEL:
            cmd += ["-accel", ACCEL]
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
        self.monitor = None
        if monitor:
            self.monitor = Serial(socket.create_connection(("127.0.0.1", monitor), timeout=5), [])
            self.monitor.read_until(b"(qemu) ")

    def screen(self):
        """The VGA text screen through the monitor: 25 rows of
        (text, attributes), the text in code page 437."""
        self.monitor.sock.sendall(b"xp /2000hx 0xb8000\n")
        out = self.monitor.read_until(b"(qemu) ", timeout=20).decode("latin-1")
        cells = [int(w, 16) for line in out.splitlines() if ": 0x" in line
                 for w in line.split(":", 1)[1].split()]
        rows = []
        for r in range(25):
            row = cells[r * 80:(r + 1) * 80]
            rows.append((bytes(c & 0xff for c in row).decode("cp437"),
                         [c >> 8 for c in row]))
        return rows

    def monitor_command(self, line, timeout=60):
        self.monitor.sock.sendall(line.encode() + b"\n")
        return self.monitor.read_until(b"(qemu) ", timeout=timeout).decode("latin-1")

    def pixels(self, work):
        """The screen as the monitor dumps it: (width, height, RGB bytes)."""
        path = os.path.join(work, "screen.ppm")
        if os.path.exists(path):
            os.remove(path)
        self.monitor_command("screendump " + path.replace("\\", "/"))
        for _ in range(50):
            if os.path.exists(path) and os.path.getsize(path) > 15:
                break
            time.sleep(0.1)
        with open(path, "rb") as f:
            data = f.read()
        magic, size, depth, rest = data.split(b"\n", 3)
        w, h = (int(x) for x in size.split())
        return w, h, rest

    def keys(self, text):
        """Types text on the PS/2 keyboard (QEMU's sendkey)."""
        for ch in text:
            name = SENDKEY.get(ch)
            if name is None:
                name = "shift-" + ch.lower() if ch.isupper() else ch
            self.monitor_command("sendkey " + name)
            time.sleep(0.05)

    def close(self):
        if self.proc.poll() is None:
            self.proc.kill()
        self.serial.sock.close()
        if self.monitor:
            self.monitor.sock.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--qemu")
    ap.add_argument("--transcript")
    ap.add_argument("--port", type=int, default=45321)
    ap.add_argument("--no-install", action="store_true",
                    help="no install onto a second disk")
    ap.add_argument("--no-mbl", action="store_true",
                    help="no editing with Mbl across a reboot")
    ap.add_argument("--no-novgorod", action="store_true",
                    help="no Novgorod over the network")
    ap.add_argument("--no-tls", action="store_true",
                    help="no TLS client and HTTPS server")
    ap.add_argument("--no-tasks", action="store_true",
                    help="no programs as tasks side by side")
    ap.add_argument("--no-native", action="store_true",
                    help="no Linux programs in ring 3")
    ap.add_argument("--no-ural", action="store_true",
                    help="no package install with ural over HTTPS")
    ap.add_argument("--no-gui", action="store_true",
                    help="no Kremlin, Mbltishi and Tver on the framebuffer")
    ap.add_argument("--accel", help="QEMU accelerator: whpx (Windows), kvm (Linux)")
    args = ap.parse_args()
    global ACCEL
    ACCEL = args.accel
    sys.stdout.reconfigure(errors="backslashreplace")
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
        check("the network card gets an address from DHCP",
              "net:     e1000 52:54:00:12:34:56, 10.0.2.15/24 (DHCP), gateway 10.0.2.2" in boot, boot)
        check("Murmansk loads its rules",
              "fw:      Murmansk, 3 rules from /etc/murmansk.rules; in deny, out allow" in boot, boot)
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

    if not args.no_mbl:
        mbl(qemu, disk, args.port + 10, log, check)
    if not args.no_novgorod:
        firewall(qemu, disk, args.port + 30, log, check)
        novgorod(qemu, disk, args.port + 20, log, check)
    if not args.no_tls:
        secure(qemu, disk, args.port + 40, log, check)
    if not args.no_tasks:
        tasks(qemu, disk, args.port + 60, log, check)
    if not args.no_native:
        native(qemu, disk, args.port + 70, log, check)
    if not args.no_ural:
        ural(qemu, disk, args.port + 80, log, check)
    if not args.no_gui:
        gui(qemu, disk, work, args.port + 50, log, check)
    if not args.no_install:
        failures += install(qemu, disk, work, args.port + 1, log, check)

    if args.transcript:
        with open(args.transcript, "wb") as f:
            f.write(b"".join(log))
    shutil.rmtree(work, ignore_errors=True)
    print("%d failed" % failures)
    return 1 if failures else 0


NOTE = ["Moscow, 2 November 2026: written in Mbl, kept on Belarus.",
        "Привет из Москвы"]


def mbl_frame(serial, timeout=120.0):
    """Reads one screen Mbl draws, up to the cursor's final position;
    returns its status line."""
    serial.read_until(b"\x1b[7m", timeout)
    status = serial.read_until(b"\x1b[0m\x1b[K\x1b[", timeout)
    serial.read_until(b"H", timeout)
    return status[:-len(b"\x1b[0m\x1b[K\x1b[")].decode("utf-8", "replace")


def mbl(qemu, disk, port, log, check):
    """Writes a note with the Mbl editor, src/Mbl/Mbl.su run from its
    source by russia, halts, boots again and finds the note there."""
    m = Machine(qemu, [(disk, "raw")], port, log, monitor=port + 1)
    serial = m.serial
    try:
        serial.output_to_prompt(timeout=60)
        serial.command('run(st): "Mbl" "/home/diary.txt"')
        status = mbl_frame(serial)
        check("Mbl starts on a new file", status.startswith(" NORMAL  /home/diary.txt"), repr(status))
        # Mbl reads the byte after ESC (is it an arrow key?): h, a move,
        # lets it go back to normal mode
        keys = "i" + NOTE[0] + "\r" + NOTE[1] + "\x1bh"
        serial.sock.sendall(keys.encode())
        while "NORMAL" not in status or "[+]" not in status:
            status = mbl_frame(serial)
        rows = m.screen()
        check("VGA: the first line of the note", rows[0][0].rstrip() == NOTE[0], repr(rows[0][0]))
        check("VGA: a UTF-8 character a cell", rows[1][0].rstrip() == "■■■■■■ ■■ ■■■■■■", repr(rows[1][0]))
        check("VGA: ~ past the end", rows[2][0].rstrip() == "~" and rows[23][0].rstrip() == "~",
              repr([rows[2][0], rows[23][0]]))
        check("VGA: the status line, reversed", rows[24][0].startswith(" NORMAL  /home/diary.txt [+]")
              and rows[24][1][0] == 0x70 and rows[0][1][0] == 0x07,
              "%r %x %x" % (rows[24][0], rows[24][1][0], rows[0][1][0]))
        serial.sock.sendall(b":wq\r")
        got = clean(serial.output_to_prompt(timeout=60))
        rows = m.screen()
        check("VGA: Mbl clears the screen as it quits",
              rows[0][0].rstrip() == "russia>" and rows[1][0].strip() == "", repr(rows[:2]))
        serial.command("print(_): st")
        got = clean(serial.output_to_prompt())
        check("Mbl exits with 0", got == "0", repr(got))
        serial.command("halt(_):")
        try:
            code = m.proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            code = None
        check("halt(_): stops the machine", code == 1, "exit status %r" % code)
    except (TimeoutError, EOFError) as e:
        check("Mbl conversation", False, str(e))
    finally:
        m.close()

    # the next boot: the note is on the disk
    m = Machine(qemu, [(disk, "raw")], port + 2, log, monitor=port + 3)
    serial = m.serial
    try:
        serial.output_to_prompt(timeout=60)
        serial.command('cat(_): "/home/diary.txt"')
        got = clean(serial.output_to_prompt())
        check("after the reboot the note is there", got == "\n".join(NOTE), repr(got))
        serial.command('run(st): "/src/ussr/src/Mbl/Mbl.su" "/home/diary.txt"')
        status = mbl_frame(serial)
        rows = m.screen()
        check("Mbl opens the note", status.startswith(" NORMAL  /home/diary.txt    ")
              and rows[0][0].rstrip() == NOTE[0], repr([status, rows[0][0]]))
        serial.sock.sendall(b":q\r")
        serial.output_to_prompt(timeout=60)
        # the shell's own struct types live on under the ones the
        # editor's russia declared and gave back
        serial.command('print(_): "the shell is well"')
        got = clean(serial.output_to_prompt())
        check("the shell is well after the editor", got == "the shell is well", repr(got))
        serial.command("fsck(_):")
        got = clean(serial.output_to_prompt())
        check("the disk checks clean after it",
              re.fullmatch(r"\d+ directories, \d+ files, \d+ bytes; \d+ inodes, \d+ block map nodes, map depth up to \d+", got)
              is not None, repr(got))
        serial.sock.sendall(bytes([4]))
        serial.read_until(b"system halted", timeout=20)
    except (TimeoutError, EOFError) as e:
        check("Mbl after the reboot", False, str(e))
    finally:
        m.close()


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args):
        return None


def http(url, data=None, headers=None, timeout=120):
    """(status, headers, body) of a request, redirects not followed."""
    opener = urllib.request.build_opener(NoRedirect)
    req = urllib.request.Request(url, data=data, headers=headers or {})
    try:
        with opener.open(req, timeout=timeout) as r:
            return r.status, r.headers, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


POST_TEXT = "Served by **Moscow**: Ukraine over the kernel's own TCP/IP."


def firewall(qemu, disk, port, log, check):
    """A server on a port no rule opens: Ukraine listens, and a client
    of this machine gets nothing, Murmansk dropping its SYN."""
    web = port + 4
    m = Machine(qemu, [(disk, "raw")], port, log, forward=[(web, 18992)])
    serial = m.serial
    try:
        serial.output_to_prompt(timeout=60)
        serial.command('run(st): "/src/ussr/tests/ukraine_http.su"')
        said = clean(serial.read_until(b"ukraine: listening on", timeout=600))
        check("Ukraine listens on 18992", "ukraine: listening on" in said, said)
        try:
            status, headers, body = http("http://127.0.0.1:%d/health" % web, timeout=15)
            got = "HTTP %r %r" % (status, body[:60])
        except OSError as e:
            got = None
        check("Murmansk keeps a port no rule opens shut", got is None, repr(got))
    except (TimeoutError, EOFError) as e:
        check("firewall conversation", False, str(e))
    finally:
        m.close()


NOVGOROD = ('run(st): "novgorod" "--http" "--host" "0.0.0.0" "--port" "8080" '
            '"--data" "/home/novgorod" "--static" "/src/ussr/src/novgorod/static" '
            '"--kvdb" "/home/novgorod/posts.kv"')


def novgorod(qemu, disk, port, log, check):
    """Starts Novgorod in Moscow, its posts in a Dubna KV store, with host
    port port + 4 forwarded to its 8080; reads and posts over HTTP, then
    boots again and reads the post back from the store."""
    web = port + 4
    base = "http://127.0.0.1:%d" % web
    m = Machine(qemu, [(disk, "raw")], port, log, forward=[(web, 8080)])
    serial = m.serial
    try:
        serial.output_to_prompt(timeout=60)
        serial.command(NOVGOROD)
        t0 = time.time()
        said = clean(serial.read_until(b"ukraine: listening on 0.0.0.0", timeout=1200))
        check("Novgorod starts from its source and listens (%.0f s)" % (time.time() - t0),
              "novgorod: 0 posts in /home/novgorod" in said, said)
        status, headers, body = http(base + "/")
        page = body.decode("utf-8", "replace")
        check("GET / over Moscow's TCP", status == 200
              and '<a href="/c/">/c/ C and systems</a>' in page, "%r %r" % (status, page[:300]))
        status, headers, body = http(base + "/static/novgorod.css")
        with open(os.path.join(ROOT, "src", "novgorod", "static", "novgorod.css"), "rb") as f:
            css = f.read()
        check("GET /static/novgorod.css, %d bytes in segments" % len(css),
              status == 200 and body == css, "%r %d bytes" % (status, len(body)))
        boundary = "----moscowtest"
        fields = [("board", "c"), ("thread", "0"), ("subject", "Moscow"),
                  ("text", POST_TEXT), ("website", ""), ("mime", ""),
                  ("imgw", ""), ("imgh", ""), ("img", "")]
        form = "".join('--%s\r\nContent-Disposition: form-data; name="%s"\r\n\r\n%s\r\n'
                       % (boundary, k, v) for k, v in fields) + "--%s--\r\n" % boundary
        status, headers, body = http(base + "/post", form.encode(),
                                     {"Content-Type": "multipart/form-data; boundary=" + boundary})
        check("POST /post makes thread 1", status == 303
              and headers.get("Location") == "/c/res/1#p1", "%r %r %r" % (status, dict(headers), body[:300]))
        status, headers, body = http(base + "/c/res/1")
        page = body.decode("utf-8", "replace")
        check("the thread shows the post", status == 200 and "Moscow" in page
              and "Served by <b>Moscow</b>" in page, "%r %r" % (status, page[-1500:]))
        status, headers, body = http(base + "/nothing/here/at/all")
        check("an unknown page is 404", status == 404, repr(status))
    except (TimeoutError, EOFError, OSError) as e:
        check("Novgorod over the network", False, str(e))
    finally:
        # the server runs until it is stopped: the machine is turned off
        m.close()

    m = Machine(qemu, [(disk, "raw")], port + 2, log, forward=[(web, 8080)])
    serial = m.serial
    try:
        serial.output_to_prompt(timeout=60)
        serial.command("fsck(_):")
        got = clean(serial.output_to_prompt())
        check("the disk checks clean after it",
              re.fullmatch(r"\d+ directories, \d+ files, \d+ bytes; \d+ inodes, \d+ block map nodes, map depth up to \d+", got)
              is not None, repr(got))
        serial.command('ls(_): "/home/novgorod/posts"')
        got = clean(serial.output_to_prompt())
        check("no post files: the post is in the store", got == "", repr(got))
        serial.command('bsize(n): "/home/novgorod/posts.kv"')
        serial.output_to_prompt()
        serial.command("print(_): {n % 4096 = 0 && n >= 4 * 4096}")
        got = clean(serial.output_to_prompt())
        check("the store is whole pages: metas, a leaf, the free list, the value", got == "true", repr(got))
        serial.command(NOVGOROD)
        said = clean(serial.read_until(b"ukraine: listening on 0.0.0.0", timeout=1200))
        check("after the reboot Novgorod finds the post in the store",
              "novgorod: 1 posts in /home/novgorod" in said, said)
        status, headers, body = http(base + "/c/res/1")
        page = body.decode("utf-8", "replace")
        check("the thread is served again", status == 200
              and "Served by <b>Moscow</b>" in page, "%r %r" % (status, page[-1500:]))
    except (TimeoutError, EOFError, OSError) as e:
        check("Novgorod's post after the reboot", False, str(e))
    finally:
        m.close()


CERT = os.path.join(ROOT, "tests", "ryazan", "localhost.cert.pem")
KEY = os.path.join(ROOT, "tests", "ryazan", "localhost.key.pem")


class Hello(BaseHTTPRequestHandler):
    def do_GET(self):
        body = b"hello from the host"
        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


def shell_kib(text):
    m = re.search(r"^ +0  run +\d+ +(\d+)  moscow$", text, re.M)
    return int(m.group(1)) if m else 0


def kv_kib(say):
    """KiB of the kernel's value heap mapped so far: its pages stay"""
    say("k_gc(kvg):")
    say("at(kvm): kvg 2")
    got = say("print(_): {kvm / 1024}")
    m = re.search(r"([\d.]+)\s*$", got)
    return float(m.group(1)) if m else 0


def free_kib(text):
    m = re.search(r"free (\d+) KiB", text)
    return int(m.group(1)) if m else -1


def tasks(qemu, disk, port, log, check):
    """Tasks: a program that never waits is switched out by the timer, so
    the shell answers; a program next to the shell runs, ends and gives
    its status and its memory back; kill ends one; Novgorod serves from a
    task of its own while the shell uses the same network; the kernel's
    collector keeps its heap small. fg gives a task the console (Ctrl-Z,
    Ctrl-C); limit ends a task over it; prio gives more CPU."""
    web = port + 4
    base = "http://127.0.0.1:%d" % web
    m = Machine(qemu, [(disk, "raw")], port, log, forward=[(web, 8080)])
    serial = m.serial

    def say(line, timeout=120):
        serial.command(line)
        return clean(serial.output_to_prompt(timeout=timeout))

    try:
        serial.output_to_prompt(timeout=60)
        got = say("tasks(_):")
        check("tasks(_): the kernel's task", re.search(r"^ +0  run +\d+ +\d+  moscow$", got, re.M)
              is not None, got)
        before = free_kib(say("mem(_):"))
        shell_before = shell_kib(say("tasks(_):"))
        kv_before = kv_kib(say)
        got = say('spawn(b): "/src/ussr/tests/moscow_busy.su"')
        check("spawn starts a task", got == "[1] /src/ussr/tests/moscow_busy.su", got)
        time.sleep(5)
        t0 = time.time()
        got = say('print(_): "the shell answers"', timeout=60)
        check("the shell answers next to a task that never waits (%.1f s)" % (time.time() - t0),
              got == "the shell answers", got)
        got = say("tasks(_):")
        busy = re.search(r"^ +1  run +(\d+) +(\d+)  /src/ussr/tests/moscow_busy\.su$", got, re.M)
        check("the busy task has had the CPU, in an address space of its own",
              busy is not None and int(busy.group(1)) > 500 and int(busy.group(2)) > 0, got)
        got = say('spawn(t): "/src/ussr/tests/moscow_tick.su"')
        check("a second task", got == "[2] /src/ussr/tests/moscow_tick.su", got)
        got = say("wait(st): t", timeout=300)
        got += "\n" + say("print(_): st")
        check("the ticking task runs, reads the end of its input, ends with its status",
              got.count("tick") == 6 and "\n-1\n" in "\n" + got and got.endswith("\n7"), got)
        got = say("kill(r): b")
        got += "\n" + say("wait(st): b") + "\n" + say("print(_): st")
        check("kill ends the busy task: status 137", got.endswith("137"), got)
        got = say("tasks(_):")
        check("only the kernel's task is left", got.count("\n") == 1, got)
        after = free_kib(say("mem(_):"))
        shell_after = shell_kib(say("tasks(_):"))
        kv_after = kv_kib(say)
        # what the shell itself took meanwhile (its REPL lines) and the
        # pages the kernel's value heap mapped (they stay its) aside
        lost = before - after - (shell_after - shell_before) - (kv_after - kv_before)
        check("the tasks' memory is given back (%d KiB unaccounted)" % lost,
              before > 0 and abs(lost) < 4096,
              "free %d -> %d, the shell %d -> %d, kernel values %d -> %d KiB"
              % (before, after, shell_before, shell_after, kv_before, kv_after))
        got = say("kill(r): 0")
        check("task 0 is not to be killed", "kill: no task 0 to end" in got, got)
        # the console to a task: fg; Ctrl-Z gives it back, Ctrl-C ends it
        say('spawn(e): "/src/ussr/tests/moscow_echo.su"')
        serial.command("fg(st): e")
        time.sleep(2)
        serial.sock.sendall(b"hello\r")
        got = clean(serial.read_until(b"echo: hello", timeout=60))
        check("fg: the task reads the console", got.endswith("echo: hello"), got)
        serial.sock.sendall(b"bye\r")
        serial.output_to_prompt(timeout=60)
        got = say("print(_): st")
        check("and fg gives its status when it ends", got == "3", got)
        say('spawn(e): "/src/ussr/tests/moscow_echo.su"')
        serial.command("fg(st): e")
        time.sleep(2)
        serial.sock.sendall(bytes([26]))
        got = clean(serial.output_to_prompt(timeout=60))
        got += "\n" + say("print(_): st")
        check("Ctrl-Z gives the console back, the task goes on in the background",
              "^Z" in got and "goes on in the background" in got and "-2" in got.split("\n"), got)
        got = say("wait(st): e", timeout=60) + "\n" + say("print(_): st")
        check("in the background it reads the end of its input",
              got.endswith("5"), got)
        say('spawn(b): "/src/ussr/tests/moscow_busy.su"')
        serial.command("fg(st): b")
        time.sleep(2)
        serial.sock.sendall(bytes([3]))
        got = clean(serial.output_to_prompt(timeout=60))
        got += "\n" + say("print(_): st")
        check("Ctrl-C ends the console's task, even one that never reads: 130",
              "^C" in got and got.endswith("130"), got)
        # a memory limit
        say('spawn(g): "/src/ussr/tests/moscow_grow.su"')
        say("limit(_): g 16384")
        got = say("wait(st): g", timeout=120) + "\n" + say("print(_): st")
        check("a task over its memory limit ends: status 1",
              "went over its memory limit" in got and got.endswith("\n1"), got)
        # priorities: a slice of 4 ticks against 1
        say('spawn(b1): "/src/ussr/tests/moscow_busy.su"')
        say('spawn(b2): "/src/ussr/tests/moscow_busy.su"')
        say("prio(_): b2 4")

        def cpu(text, name):
            m = re.search(r"^ +%s  run +(\d+) " % name, text, re.M)
            return int(m.group(1)) if m else 0
        got0 = say("tasks(_):")
        ids = re.findall(r"^ +(\d+)  run .*moscow_busy\.su$", got0, re.M)
        time.sleep(8)
        got1 = say("tasks(_):")
        say("kill(_): b1")
        say("kill(_): b2")
        if len(ids) == 2:
            d1 = cpu(got1, ids[0]) - cpu(got0, ids[0])
            d2 = cpu(got1, ids[1]) - cpu(got0, ids[1])
        else:
            d1 = d2 = 0
        check("prio 4 gets about 4 times the CPU of prio 1 (%d ms, %d ms)" % (d2, d1),
              d1 > 0 and 2.5 < d2 / d1 < 6, got0 + "\n" + got1)
        # Novgorod in a task: the network is its own
        serial.command(NOVGOROD.replace("run(st):", "spawn(nv):"))
        said = clean(serial.read_until(b"ukraine: listening on 0.0.0.0", timeout=1200))
        check("Novgorod listens from a task of its own",
              re.search(r"novgorod: \d+ posts", said) is not None, said)
        status, headers, body = http(base + "/")
        check("GET / from Novgorod's task", status == 200, repr(status))
        got = say('ping(n): "10.0.2.2" 1')
        check("the shell pings while Novgorod serves: one network stack for both",
              "reply from 10.0.2.2: seq 1" in got, got)
        status, headers, body = http(base + "/")
        check("and Novgorod still answers", status == 200, repr(status))
        say("kill(r): nv")
        try:
            status, headers, body = http(base + "/", timeout=10)
            gone = False
        except OSError:
            gone = True
        check("killed, its socket goes with it", gone, "still answered %r" % (status,))
        serial.command('spawn(nv): "novgorod" "--http" "--host" "0.0.0.0" "--port" "8080" '
                       '"--data" "/home/novgorod" "--static" "/src/ussr/src/novgorod/static" '
                       '"--kvdb" "/home/novgorod/posts.kv"')
        serial.read_until(b"listening", timeout=240)
        status, headers, body = http(base + "/")
        check("and its port is free: a new Novgorod listens on it", status == 200, repr(status))
        say("kill(r): nv")
        got = say('ping(n): "10.0.2.2" 1', timeout=60)
        check("the network goes on", "reply from 10.0.2.2: seq 1" in got, got)
        # the kernel's collector: kernel calls that leave garbage
        got = say('run(_): "/src/ussr/tests/moscow_garbage.su"', timeout=900)
        runs = re.search(r"garbage: collections\n(\d+)", got)
        used = re.search(r"garbage: kernel values in use, KiB\n([\d.]+)", got)
        check("the kernel's collector keeps its heap small (%s collections, %s KiB in use)"
              % (runs and runs.group(1), used and used.group(1)),
              runs is not None and int(runs.group(1)) > 0 and used is not None
              and float(used.group(1)) < 65536, got)
        serial.sock.sendall(bytes([4]))
        serial.read_until(b"system halted", timeout=20)
    except (TimeoutError, EOFError, OSError) as e:
        check("tasks conversation", False, str(e))
    finally:
        m.close()


def native(qemu, disk, port, log, check):
    """Linux programs: a static PIE ELF program runs in ring 3 and the kernel
    answers its system calls. The test program goes through memory, time and
    files; a bad pointer ends only it; the timer takes the CPU from one that
    never waits; limit and Ctrl-C reach it. Then ussr itself, the same C as on
    a host: a script, -c for a .ru that Moscow's VM runs, its REPL."""
    m = Machine(qemu, [(disk, "raw")], port, log)
    serial = m.serial

    def say(line, timeout=120):
        serial.command(line)
        return clean(serial.output_to_prompt(timeout=timeout))

    try:
        serial.output_to_prompt(timeout=60)
        say('file_exists(f): "/bin/ussr"')
        if say("print(_): f") != "true":
            print("SKIP no /bin/ussr: built on a Linux host (build.sh)")
            return
        before = free_kib(say("mem(_):"))
        shell_before = shell_kib(say("tasks(_):"))
        kv_before = kv_kib(say)
        got = say('run(st): "/bin/moscow_native" "x"')
        check("a Linux program runs: arguments, environment, malloc, mmap, SSE",
              "native: 2 args, argv0 /bin/moscow_native, HOME=/home" in got
              and "native: malloc 7" in got and "native: mmap 9" in got
              and "native: real 20" in got and "native: clock ok" in got, got)
        check("its files: write, read back, stat, a directory, an error",
              "native: read back: written by a Linux program" in got
              and "native: stat size 27, dir 0" in got and "native: /etc has motd: 1" in got
              and "native: binary 8 0 200 122" in got and "native: pread 200" in got
              and "native: open of nothing: errno 2" in got
              and "native: cwd /home/nd" in got, got)
        got = say("print(_): st")
        check("its exit status", got == "11", got)
        got = say('cat(_): "/home/native.txt"')
        check("what it wrote is on the disk for the kernel",
              "written by a Linux program" in got, got)
        after = free_kib(say("mem(_):"))
        shell_after = shell_kib(say("tasks(_):"))
        kv_after = kv_kib(say)
        lost = before - after - (shell_after - shell_before) - (kv_after - kv_before)
        check("its memory comes back (%d KiB unaccounted)" % lost, abs(lost) < 4096,
              "free %d -> %d, the shell %d -> %d, kernel values %d -> %d KiB"
              % (before, after, shell_before, shell_after, kv_before, kv_after))
        serial.command('run(st): "/bin/moscow_native" "read"')
        serial.read_until(b"native: cwd", timeout=120)
        time.sleep(2)
        serial.sock.sendall(b"typed here\r")
        got = clean(serial.output_to_prompt(timeout=120))
        check("it reads the console", "native: stdin: typed here" in got, got)
        got = say('run(st): "/bin/moscow_native" "bad"')
        got += "\n" + say("print(_): st")
        check("a bad pointer ends only it: status 139",
              "the program stopped: exception 14" in got and got.endswith("139"), got)
        say('spawn(b): "/bin/moscow_native" "busy"')
        time.sleep(4)
        got = say('print(_): "the shell answers"', timeout=60)
        # (what the program printed before its loop may come in between)
        check("the timer takes the CPU from a program in ring 3",
              got.endswith("the shell answers"), got)
        serial.command("fg(st): b")
        time.sleep(2)
        serial.sock.sendall(bytes([3]))
        got = clean(serial.output_to_prompt(timeout=60))
        got += "\n" + say("print(_): st")
        check("Ctrl-C ends it: 130", "^C" in got and got.endswith("130"), got)
        say('spawn(g): "/bin/moscow_native" "grow"')
        say("limit(_): g 16384")
        got = say("wait(st): g", timeout=120) + "\n" + say("print(_): st")
        check("over its memory limit it ends: status 1",
              "went over its memory limit" in got and got.endswith("\n1"), got)
        say('writefile(_): "/home/h.su" "print(_): \\"ussr on Moscow\\"\\nset(s): 0\\n'
            'set(i): 0\\nwhile(_): {i < 1000} [\\n set(s): {s + i}\\n set(i): {i + 1}\\n]\\n'
            'print(_): s\\n"')
        got = say('run(st): "ussr" "/home/h.su"') + "\n" + say("print(_): st")
        check("ussr runs a script", got == "ussr on Moscow\n499500\n0", got)
        say('run(st): "ussr" "-c" "/home/h.su" "/home/h.ru"')
        got = say('run(st): "/home/h.ru"')
        check("ussr -c writes a program Moscow's VM runs",
              got == "ussr on Moscow\n499500", got)
        serial.command('run(st): "ussr"')
        serial.read_until(b"ussr> ", timeout=120)
        serial.sock.sendall(b'print(_): "in the repl"\r')
        got = clean(serial.read_until(b"ussr> ", timeout=60))
        check("ussr's REPL", "in the repl" in got, got)
        serial.sock.sendall(bytes([4]))
        serial.output_to_prompt(timeout=60)
        got = say("print(_): st")
        check("and Ctrl-D ends it", got == "0", got)
        serial.sock.sendall(bytes([4]))
        serial.read_until(b"system halted", timeout=20)
    except (TimeoutError, EOFError, OSError) as e:
        check("Linux programs conversation", False, str(e))
    finally:
        m.close()


class Repository(SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def host_belarus(*args):
    """belarus.su on this machine, through the ussr that built the disk."""
    ussr = os.path.join(ROOT, "ussr.exe" if os.name == "nt" else "ussr")
    env = dict(os.environ, USSR_KIEV="", USSR_MOSCOW="")
    subprocess.run([ussr, os.path.join("src", "belarus", "belarus.su")] + list(args),
                   cwd=ROOT, env=env, check=True, stdout=subprocess.DEVNULL)


def ural(qemu, disk, port, log, check):
    """Ural, the package manager, as on a host: the same C, with SQLite, curl
    and OpenSSL in it. On Moscow it packs a repository; this machine serves
    those files over HTTPS (QEMU shows it as 10.0.2.2, /etc/hosts calls it
    ural.test); after a reboot Ural refuses the server while its certificate
    is not trusted, then installs from it, and uninstalls. On a copy of the
    disk: the other phases do not see its /etc/hosts or its test certificate."""
    work = tempfile.mkdtemp(prefix="moscow_ural_")
    copy = os.path.join(work, "ural.img")
    shutil.copyfile(disk, copy)
    site = os.path.join(work, "site", "repo")
    os.makedirs(site)
    handler = functools.partial(Repository, directory=os.path.join(work, "site"))
    server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(os.path.join(ROOT, "tests", "ural", "ural.test.cert.pem"),
                        os.path.join(ROOT, "tests", "ural", "ural.test.key.pem"))
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    url = "https://ural.test:%d/repo" % server.server_address[1]

    m = Machine(qemu, [(copy, "raw")], port, log)
    serial = m.serial

    def say(line, timeout=300):
        serial.command(line)
        return clean(serial.output_to_prompt(timeout=timeout))

    try:
        serial.output_to_prompt(timeout=60)
        say('file_exists(f): "/bin/ural"')
        if say("print(_): f") != "true":
            print("SKIP no /bin/ural: built on a Linux host (build.sh)")
            return
        got = say('run(st): "ural" "init"') + "\n" + say("print(_): st")
        check("ural starts and makes its SQLite database", got.endswith("0"), got)
        say('writefile(_): "/home/repo/hello/box.uno" "\'Package{name=`hello`,version=`1.0.0`,'
            'description=`a test`,entry=`hello.su`,dependencies=[],files=[],'
            'license=`MIT`,minimum_ussr=`0.1.0`,source=``}\'"')
        say('writefile(_): "/home/repo/hello/hello.su" "print(_): \\"hello from a package\\"\\n"')
        got = say('run(st): "ural" "pack" "/home/repo" "%s"' % url)
        got += "\n" + say("print(_): st")
        check("ural pack makes a repository: ZIP, manifest, SQLite index", got.endswith("0"), got)
        got = say('ls(_): "/home/repo"')
        check("the repository's files are on the disk",
              "hello-1.0.0.ural" in got and "hello-box.uno" in got and "ussr.boxes" in got, got)
        serial.sock.sendall(bytes([4]))
        serial.read_until(b"system halted", timeout=20)
        m.proc.wait(timeout=20)
        m.close()
        for name in re.findall(r"^- +\d+  (\S+)$", got, re.M):
            host_belarus("get", copy, "/home/repo/" + name, os.path.join(site, name))

        m = Machine(qemu, [(copy, "raw")], port, log)
        serial = m.serial
        serial.output_to_prompt(timeout=60)
        say('writefile(_): "/etc/hosts" "10.0.2.2 ural.test\\n"')
        got = say('run(st): "ural" "server" "add" "%s"' % url)
        got += "\n" + say("print(_): st")
        check("ural server add", got.endswith("0"), got)
        got = say('run(st): "ural" "install" "hello"') + "\n" + say("print(_): st")
        check("a certificate it does not trust stops ural", not got.endswith("\n0"), got)
        say('readfile(a): "/etc/ssl/certs.pem"')
        say('readfile(b): "/src/ussr/tests/ural/ural.test.cert.pem"')
        say("concat(c): a b")
        say('writefile(_): "/etc/ssl/certs.pem" c')
        got = say('run(st): "ural" "search" "hello"')
        check("ural search reads the index over HTTPS", "hello" in got, got)
        got = say('run(st): "ural" "install" "hello"') + "\n" + say("print(_): st")
        check("ural install: name lookup, HTTPS, hashes, ZIP, its records",
              got.endswith("\n0"), got)
        got = say('run(st): "ural" "list"')
        check("ural list has it", "hello" in got and "1.0.0" in got, got)
        got = say('run(st): "ussr" "/home/.ussr/packages/hello/hello.su"')
        check("what it installed runs", got == "hello from a package", got)
        got = say('run(st): "ural" "uninstall" "hello"') + "\n" + say("print(_): st")
        check("ural uninstall", got.endswith("0"), got)
        say('file_exists(f): "/home/.ussr/packages/hello/hello.su"')
        got = say("print(_): f")
        check("and the file is gone", got == "false", got)
        serial.sock.sendall(bytes([4]))
        serial.read_until(b"system halted", timeout=20)
    except (TimeoutError, EOFError, OSError, subprocess.SubprocessError) as e:
        check("ural conversation", False, str(e))
    finally:
        m.close()
        server.shutdown()
        shutil.rmtree(work, ignore_errors=True)


def gui(qemu, disk, work, port, log, check):
    """Windows on the framebuffer: Kremlin, the desktop; Mbltishi, the
    terminal, started from its panel, with the shell in it; Tver, the
    file manager, over the Belarus root; then text mode again. The mouse
    starts in the middle of the screen and QEMU moves it by pixels."""
    m = Machine(qemu, [(disk, "raw")], port, log, monitor=port + 1)
    serial = m.serial

    def wait_for(label, test, timeout=600):
        end = time.time() + timeout
        while True:
            shot = m.pixels(work)
            if test(shot):
                check(label, True)
                return True
            if time.time() > end:
                w, h, _ = shot
                check(label, False, "screen %dx%d" % (w, h))
                return False
            time.sleep(2)

    def dark(c):
        return max(c) < 12

    def click(dx, dy):
        # in steps: QEMU's PS/2 mouse carries a burst of packets only so
        # far, and a button sent meanwhile lands short of the target
        steps = max(1, (max(abs(dx), abs(dy)) + 99) // 100)
        for i in range(steps):
            m.monitor_command("mouse_move %d %d" % (dx * (i + 1) // steps - dx * i // steps,
                                                    dy * (i + 1) // steps - dy * i // steps))
            time.sleep(0.3)
        time.sleep(1.0)
        m.monitor_command("mouse_button 1")
        time.sleep(0.2)
        m.monitor_command("mouse_button 0")

    try:
        boot = clean(serial.output_to_prompt(timeout=60))
        check("the kernel loads Petersburg",
              "gui:     Petersburg from /lib/gui.so, 1024x768" in boot, boot)
        serial.sock.sendall(b'run(_): "kremlin"\r')
        # the panel's KREMLIN button, red, at the bottom left
        if not wait_for("Kremlin's desktop fills the 1024x768 screen",
                        lambda s: s[0] == 1024 and s[1] == 768 and pixel(s, 80, 745) == (179, 38, 30)):
            return
        # the Terminal pin: Mbltishi with the shell, whose console the
        # serial line has too
        click(-390, 369)
        said = clean(serial.read_until(b"Enter a command list or press Ctrl-D to exit.", timeout=600))
        check("Kremlin's Terminal starts Mbltishi with the USSR shell",
              "USSR Unified Shell Script REPL" in said, said)
        wait_for("Mbltishi's window is over the desktop",
                 lambda s: dark(pixel(s, 500, 330)) and not dark(pixel(s, 1000, 20)), 120)
        m.keys('print(_): "hello world"\n')
        said = clean(serial.read_until(b"\r\nhello world\r\n", timeout=120))
        check("the shell in Mbltishi runs what is typed in its window", "hello world" in said, said)
        # Kremlin launched it as a task of its own, and goes on next to it
        m.keys("tasks(_):\n")
        said = clean(serial.read_until(b"Mbltishi.su\r\n", timeout=120))
        check("Mbltishi is a task of its own next to Kremlin's",
              re.search(r"^ +0  (run|wait) .*moscow$", said, re.M) is not None
              and re.search(r"^ +\d+  run .*Mbltishi\.su$", said, re.M) is not None, said)
        m.monitor_command("sendkey ctrl-d")
        wait_for("Mbltishi closes with the shell",
                 lambda s: pixel(s, 500, 330) != (0, 0, 0) and pixel(s, 80, 745) == (179, 38, 30), 300)
        # the Files pin: Tver on the root
        click(78, 0)
        wait_for("Kremlin's Files starts Tver over the Belarus root",
                 lambda s: pixel(s, 300, 400) == (18, 20, 28) and pixel(s, 700, 400) == (18, 20, 28), 600)
        # its close box
        click(748, -680)
        wait_for("Tver closes with its window's box",
                 lambda s: pixel(s, 300, 400) != (18, 20, 28), 300)
        # Ctrl+Q, then Enter in the dialog: the shell, in text mode
        # (the prompts of the shell that ran in Mbltishi are not this one)
        serial.buf = b""
        m.monitor_command("sendkey ctrl-q")
        time.sleep(5)
        m.monitor_command("sendkey ret")
        serial.output_to_prompt(timeout=300)
        time.sleep(1)
        rows = m.screen()
        check("Kremlin exits to the shell, in text mode again",
              any(text.startswith("russia>") for text, _ in rows),
              "\n".join(text for text, _ in rows))
        serial.sock.sendall(bytes([4]))
        serial.read_until(b"system halted", timeout=20)
    except (TimeoutError, EOFError) as e:
        check("GUI conversation", False, str(e))
    finally:
        m.close()


def secure(qemu, disk, port, log, check):
    """TLS both ways: Moscow's client against an HTTPS server here (QEMU
    shows this machine as 10.0.2.2), and Novgorod over HTTPS."""
    server = HTTPServer(("127.0.0.1", 0), Hello)
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(CERT, KEY)
    server.socket = ctx.wrap_socket(server.socket, server_side=True)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    host_port = server.server_address[1]
    web = port + 4
    base = "https://127.0.0.1:%d" % web
    client = ssl.create_default_context(cafile=CERT)
    m = Machine(qemu, [(disk, "raw")], port, log, forward=[(web, 8443)])
    serial = m.serial
    try:
        boot = clean(serial.output_to_prompt(timeout=60))
        check("the kernel loads BearSSL", "tls:     BearSSL from /lib/tls.so" in boot, boot)
        serial.command('run(st): "/src/ussr/tests/moscow_tls_client.su" "%d"' % host_port)
        got = clean(serial.output_to_prompt(timeout=600))
        check("Moscow, a TLS client, checks the host's certificate",
              re.search(r"PASS handshake: TLS 1\.2, cipher suite 0x[0-9A-F]{4}", got) is not None, repr(got))
        check("and reads its page over TLS", "PASS response hello from the host" in got, repr(got))
        serial.command('run(st): "novgorod" "--host" "0.0.0.0" "--port" "8443" "--data" "/home/novgorod" '
                       '"--static" "/src/ussr/src/novgorod/static" "--kvdb" "/home/novgorod/posts.kv" '
                       '"--cert" "/src/ussr/tests/ryazan/localhost.cert.pem" '
                       '"--secret" "/src/ussr/tests/ryazan/localhost.key.pem"')
        said = clean(serial.read_until(b"ukraine: listening (HTTPS) on 0.0.0.0", timeout=1200))
        check("Novgorod listens over HTTPS", "listening (HTTPS)" in said, said)
        status, headers, body = https(base + "/", client)
        check("GET / over HTTPS, the certificate checked here", status == 200
              and '<a href="/c/">/c/ C and systems</a>' in body.decode("utf-8", "replace"), repr(status))
        boundary = "----moscowtls"
        fields = [("board", "prog"), ("thread", "0"), ("subject", "TLS"),
                  ("text", "Encrypted by **BearSSL** in Moscow."), ("website", ""),
                  ("mime", ""), ("imgw", ""), ("imgh", ""), ("img", "")]
        form = "".join('--%s\r\nContent-Disposition: form-data; name="%s"\r\n\r\n%s\r\n'
                       % (boundary, k, v) for k, v in fields) + "--%s--\r\n" % boundary
        status, headers, body = https(base + "/post", client, form.encode(),
                                      {"Content-Type": "multipart/form-data; boundary=" + boundary})
        location = headers.get("Location") if headers else None
        check("POST over HTTPS", status == 303 and location is not None
              and location.startswith("/prog/res/"), "%r %r" % (status, location))
        if location:
            status, headers, body = https(base + location.split("#")[0], client)
            check("the thread over HTTPS", status == 200
                  and "Encrypted by <b>BearSSL</b> in Moscow." in body.decode("utf-8", "replace"), repr(status))
    except (TimeoutError, EOFError, OSError) as e:
        check("TLS conversation", False, str(e))
    finally:
        m.close()
        server.shutdown()


def https(url, context, data=None, headers=None, timeout=180):
    """(status, headers, body) over HTTPS checked with context, no
    redirects followed."""
    opener = urllib.request.build_opener(NoRedirect, urllib.request.HTTPSHandler(context=context))
    req = urllib.request.Request(url, data=data, headers=headers or {})
    try:
        with opener.open(req, timeout=timeout) as r:
            return r.status, r.headers, r.read()
    except urllib.error.HTTPError as e:
        return e.code, e.headers, e.read()


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
              re.search(r"\n\d+ directories, \d+ files, \d+ bytes; \d+ inodes, \d+ block map nodes, map depth up to \d+\n", text)
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
        # the desktop by default: a key on the serial line takes the shell
        boot = clean(serial.read_until(b"(a key: the shell)", timeout=60))
        serial.sock.sendall(b"\r")
        boot += "\n" + clean(serial.output_to_prompt(timeout=60))
        check("the installed system starts the desktop by default",
              "moscow: the desktop, Kremlin, in 3 seconds (a key: the shell)" in boot
              and "moscow: no desktop" in boot, boot)
        check("the installed disk boots",
              "root:    /dev/hda from sector 8192, Belarus 'comrade'" in boot
              and "USSR Moscow on kremlin, installed from ussr-install." in boot, boot)
        for line, want in [
            ('getenv(h): "HOSTNAME"', r""),
            ("print(_): h", r"kremlin"),
            ('cat(_): "/etc/hostname"', r"kremlin"),
            ('cat(_): "/etc/gui.conf"', r"screen=1024x768\ndesktop=on"),
            ('ls(_): "/src/ussr/src/belarus"', r"- +\d+  README\.md\n- +\d+  belarus\.su"),
            ('readfile(c): "/src/ussr/src/belarus/belarus.su"', r""),
            ("len(cl): c", r""),
            ("print(_): {cl > 10000}", r"true"),
            ("fsck(_):", r"\d+ directories, \d+ files, \d+ bytes; \d+ inodes, \d+ block map nodes, map depth up to \d+"),
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
