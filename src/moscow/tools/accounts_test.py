"""Install Moscow and test its login and account permissions."""
import argparse
import hashlib
import os
import shutil
import tempfile
import qemu_test as q

def main():
    parser = argparse.ArgumentParser(description="Test installed Moscow console accounts in QEMU")
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--port", type=int, default=6500)
    options = parser.parse_args()
    qemu = options.qemu
    password = "moscow-test-passphrase"
    with tempfile.TemporaryDirectory() as w:
        disk = os.path.join(w, "source.img")
        shutil.copyfile(q.IMAGE, disk)
        def check(name, yes, detail=""):
            print("PASS" if yes else "FAIL", name, flush=True)
            assert yes, detail
        q.install(qemu, disk, w, options.port, [], check, desktop=False)
        target = os.path.join(w, "target.qcow2")
        for user, port in [("ted", options.port + 10), ("alice", options.port + 12), ("bob", options.port + 14)]:
            m = q.Machine(qemu, [(target, "qcow2")], port, [])
            s = m.serial
            def line(cmd):
                s.command(cmd)
                return q.clean(s.output_to_prompt(timeout=90))
            try:
                if user == "alice":
                    q.login(s, user, "wrong-test-passphrase")
                    message = s.read_until(b"Login incorrect.", timeout=90)
                    check("wrong password rejected", b"wrong-test-passphrase" not in message)
                q.login(s, user, password)
                if user != "alice":
                    s.read_until(b"(a key: the shell)", timeout=90)
                    s.sock.sendall(b"\r")
                s.output_to_prompt(timeout=90)
                check("kernel identity for " + user, line("whoami():") == user)
                if user == "ted":
                    entry = line('cat(_): "/etc/passwd"').splitlines()[0]
                    name, role, salt, digest = entry.split(":")
                    check("PBKDF2 verifier matches Python", name == "ted" and role == "ted" and hashlib.pbkdf2_hmac("sha256", password.encode(), salt.encode(), 600000).hex() == digest)
                    for name, role in [("alice", "staff"), ("bob", "ted")]:
                        s.command(f'reguser(): "{name}" "{role}"')
                        for prompt in (b"New password: ", b"Repeat password: "):
                            s.read_until(prompt, timeout=60)
                            s.sock.sendall(password.encode() + b"\r")
                        out = q.clean(s.output_to_prompt(timeout=90))
                        check("created " + role + " account", out.strip() == "Account created.", out)
                elif user == "alice":
                    line('setenv(_): "USER" "ted"')
                    check("USER cannot spoof identity", line("whoami():") == "alice")
                    check("ordinary user cannot create admin", "master admin login required" in line('reguser(): "mallory" "ted"'))
                    check("ordinary user cannot modify passwd", "requires a master admin" in line('writefile(_): "/etc/passwd" ""'))
                    check("ordinary user cannot access raw devices", "requires a master admin" in line('bread(_): "/dev/hda" 0 1'))
                    check("ordinary user cannot escape a writable path",
                          "requires a master admin" in line('writefile(_): "/home/../etc/passwd" ""'))
                    check("ordinary user can write in home", line('writefile(_): "/home/note" "hello"') == "")
                    check("ordinary user can run a China program", line('head(): "/home/note"') == "hello")
                else:
                    check("second master admin can register", "already exists" in line('reguser(): "alice" "ted"'))
            finally:
                m.close()


if __name__ == "__main__":
    main()
