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
    ("ls(_):", r"- +\d+  \.ussr_history\nd +\d+  bin\nd +\d+  boot\nd +\d+  etc\nd +\d+  src"),
    ('ls(_): "/bin"', r"- +\d+  belarus\.ru\n- +\d+  install\.ru\n- +\d+  stalin\.ru"),
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
        novgorod(qemu, disk, args.port + 20, log, check)
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
