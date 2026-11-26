#!/usr/bin/env python3
"""Exercise the precompiled China programs on Moscow's live ISO."""
import argparse
import os
import re
import tempfile

from qemu_test import ISO, Machine, clean, find_ovmf, uefi_drives


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--port", type=int, default=6232)
    args = parser.parse_args()
    firmware = find_ovmf(args.qemu)
    if firmware is None:
        parser.error("OVMF is required")

    with tempfile.TemporaryDirectory() as work:
        extra = uefi_drives(firmware, os.path.join(work, "vars.fd"))
        machine = Machine(args.qemu, [], args.port, [], cdrom=ISO, extra=extra)
        serial = machine.serial

        def say(command, expected):
            serial.command(command)
            actual = clean(serial.output_to_prompt(timeout=60))
            if not re.fullmatch(expected, actual):
                raise AssertionError(f"{command}\nexpected {expected!r}, got {actual!r}")
            print("PASS", command, flush=True)

        try:
            boot = clean(serial.output_to_prompt(timeout=60))
            assert "root:    /dev/ram" in boot, boot
            say('file_exists(f): "/bin/ussr"', "")
            serial.command("print(_): f")
            if clean(serial.output_to_prompt()) == "true":
                say('rename_path(_): "/bin/ussr" "/bin/ussr-disabled"', "")
            say('file_exists(f): "/bin/ussr"', "")
            say("print(_): f", "false")
            say('head(_): "/etc/hostname" 1', "moscow-install")
            say('head(rc): "/etc/hostname" 1', "moscow-install")
            say("print(_): rc", "0")
            say('/bin/echo.ru(): "absolute"', "absolute")
            say('/bin/ls.ru(rc): "/etc/hostname"', r"- +\d+ +hostname")
            say("print(_): rc", "0")
            say('run(_): "china"', r"(?s)China utilities .*whoami")
            say('run(_): "mkdir" "/china-test"', "")
            say('run(_): "cd" "/china-test"', "")
            say('run(_): "pwd"', "/china-test")
            say(r'writefile(_): "/china-test/note" "alpha\nbeta\nomega"', "")
            say('run(_): "head" "note" 1', "alpha")
            say('run(_): "tail" "note" 2', "beta\nomega")
            say('run(_): "grep" "^beta$" "note"', "beta")
            say('run(st): "grep" "^missing$" "note"', "")
            say("print(_): st", "1")
            say('run(_): "wc" "note"', "3 lines  16 bytes")
            say('run(_): "cp" "note" "copy"', "")
            say('run(_): "mv" "copy" "renamed"', "")
            say('run(_): "cat" "renamed"', "alpha\nbeta\nomega")
            say('run(_): "du"', "32 bytes  /china-test")
            say('run(_): "ls"', r"(?s).*\bnote\n.*\brenamed")
            say('run(_): "touch" "empty"', "")
            say('run(_): "rm" "note" "renamed" "empty"', "")
            say('run(_): "tree"', r"(?s).*china-test.*")
            say('run(_): "cd" ".."', "")
            say('run(_): "rmdir" "china-test"', "")
            say('run(_): "date"', r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\dZ")
            say('run(_): "uname"', r"(?i).*moscow.*")
            say('run(_): "ps"', r"(?s).*STATE.*moscow.*")
            say('run(_): "whoami"', "comrade")
            say('run(_): "who"', r"comrade on .* \(Moscow console\)")
            say('run(_): "mount"', r"/dev/ram on / \(Belarus, sector \d+\)")
            say('run(_): "df"', r"(?s)root: /dev/ram.*free.*")
            say('run(_): "dmesg"', r"(?s).*Moscow runtime: entered.*root: +/dev/ram.*")
            say('run(_): "passwd"', r"passwd: Moscow has no login password database")
            say('run(_): "kill" 0', r"kill: invalid task ID")
            say('run(_): "uptime"', r"up \d+ s")
            say('run(_): "echo" "hello" "China"', "hello China")
            say('run(_): "more" "/etc/hostname"', "moscow-install")
            serial.command('run(_): "less" "/etc/hostname"')
            serial.read_until(b"[b back, q quit]")
            serial.sock.sendall(b"q")
            serial.output_to_prompt(timeout=60)
            print("PASS less exits on q", flush=True)
        finally:
            machine.close()


if __name__ == "__main__":
    main()
