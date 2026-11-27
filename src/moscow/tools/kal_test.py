"""Test the Moscow SSH console client against a local Paramiko server."""
import argparse
import hashlib
import os
import socket
import threading
import tempfile
import paramiko
from qemu_test import ISO, Machine, clean, find_ovmf, find_qemu, uefi_drives

class ShellServer(paramiko.ServerInterface):
    def __init__(self):
        self.ready = threading.Event()
    def get_allowed_auths(self, username):
        return "password"
    def check_auth_password(self, username, password):
        if (username, password) == ("alice", "kal-test-secret"):
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED
    def check_channel_request(self, kind, chid):
        return (paramiko.OPEN_SUCCEEDED if kind == "session"
                else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED)
    def check_channel_pty_request(self, channel, term, width, height, pixelwidth, pixelheight, modes):
        return True
    def check_channel_shell_request(self, channel):
        self.ready.set()
        return True

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qemu")
    parser.add_argument("--port", type=int, default=6860)
    parser.add_argument("--uefi", action="store_true")
    args = parser.parse_args()
    qemu = find_qemu(args.qemu)
    if qemu is None:
        parser.error("QEMU is required")
    with tempfile.TemporaryDirectory() as work:
        extra = ["-cpu", "max"]
        if args.uefi:
            firmware = find_ovmf(qemu)
            if firmware is None:
                parser.error("OVMF is required for --uefi")
            extra += uefi_drives(firmware, os.path.join(work, "vars.fd"))
        exercise(qemu, args.port, extra)


def exercise(qemu, serial_port, extra):
    key = paramiko.RSAKey.generate(2048)
    fingerprint = hashlib.sha256(key.asbytes()).hexdigest()
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen()
    port = listener.getsockname()[1]
    errors = []

    def serve():
        for expected_session in (False, True):
            sock, _ = listener.accept()
            transport = paramiko.Transport(sock)
            transport.add_server_key(key)
            server = ShellServer()
            try:
                transport.start_server(server=server)
                channel = transport.accept(20)
                if not expected_session:
                    assert channel is None
                    continue
                assert channel is not None and server.ready.wait(10)
                channel.send(b"kal remote ready\r\n")
                channel.settimeout(15)
                message = bytearray()
                while not message.endswith(b"\r"):
                    part = channel.recv(128)
                    if not part:
                        raise AssertionError("connection closed before terminal input")
                    message.extend(part)
                assert message == b"hello from Moscow\r", message
                channel.send(b"kal input received\r\n")
                channel.send_exit_status(7)
                channel.shutdown_write()
                channel.close()
            except Exception as error:
                errors.append(error)
            finally:
                transport.close()

    threading.Thread(target=serve, daemon=True).start()
    machine = Machine(qemu, [], serial_port, [], cdrom=ISO, extra=extra)
    s = machine.serial
    try:
        s.output_to_prompt(timeout=90)
        command = f'kal(status): "10.0.2.2" "alice" {port}'
        s.command(f'{command} "{64 * "0"}"')
        failed = clean(s.output_to_prompt(timeout=35))
        assert "fingerprint does not match" in failed and "Password:" not in failed, failed
        s.command(f'{command} "{fingerprint}"')
        s.read_until(b"Password: ", timeout=35)
        s.sock.sendall(b"kal-test-secret\r")
        ready = s.read_until(b"kal remote ready", timeout=35)
        assert b"kal-test-secret" not in ready, "password was echoed"
        s.sock.sendall(b"hello from Moscow\r")
        output = clean(s.output_to_prompt(timeout=35))
        assert "kal input received" in output, output
        s.command("print(_): status")
        assert clean(s.output_to_prompt()) == "7"
        assert not errors, errors
        print("PASS SSH fingerprint rejection, password auth, PTY data and exit status", flush=True)
    finally:
        machine.close()
        listener.close()


if __name__ == "__main__":
    main()
