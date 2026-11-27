# Kaliningrad

Kaliningrad opens a remote SSH shell from the current console.

## Linux and Windows

Install OpenSSH Client and make sure `ssh` is on `PATH`. From the
repository root:

    ussr src/kaliningrad/kal.su server.example alice
    ussr src/kaliningrad/kal.su server.example alice 2222

Kaliningrad passes the arguments directly to OpenSSH and returns its exit
status. OpenSSH handles host verification, authentication, configuration,
and terminal I/O.

## Moscow

A Linux build of the Moscow ISO includes `/bin/kal.ru` and the static
libssh2 backend. At the Rush prompt in a master-admin session:

    kal(): "192.0.2.10" "alice" 22 "HOST_KEY_SHA256_HEX"

Use the server's IPv4 address and replace the final argument with the
64-character SHA-256 hex digest of its SSH host public key. Obtain it
from the server's trusted console or administrator. On a Linux server,
for an Ed25519 host key:

    awk '{print $2}' /etc/ssh/ssh_host_ed25519_key.pub | base64 -d | sha256sum

Use the digest of the host key the server and client negotiate. A mismatch
ends the connection before password entry. Moscow supports password
authentication and an interactive remote terminal. Press Ctrl-] to
disconnect locally. Its backend requires a CPU with working RDRAND.

With Paramiko installed for Python, run the SSH integration test after
building the ISO:

    python src/moscow/tools/kal_test.py --qemu qemu-system-x86_64 --uefi
