# ukraine.su socket/scheduler prototype (POSIX alpha)

This patch is the first runtime integration step for the `ukraine.su` HTTP
server project. It adds POSIX TCP socket built-ins and one-shot socket
readiness waits to USSR's existing cooperative scheduler. It does not yet
implement HTTP or the higher-level web framework.

## Built-ins

All calls are regular USSR commands dispatched by the VM/scheduler boundary.

- `socket_tcp(fd):` — create a nonblocking IPv4 TCP socket; returns descriptor or `-1`.
- `socket_bind(result): fd "127.0.0.1" port` — bind IPv4 address and port; `"0.0.0.0"` or `"*"` means all interfaces.
- `socket_listen(result): fd backlog` — listen on the socket.
- `socket_accept(client): fd` — accept a connection; returns descriptor or `-1`. The listener must be readable first. Accepted sockets are made nonblocking.
- `socket_nonblocking(result): fd` — set `O_NONBLOCK`.
- `socket_recv(data): fd max_bytes` — read up to 65536 bytes; returns a string, or null if the operation currently cannot read / fails. A zero-length string means the peer closed cleanly.
- `socket_send(sent): fd "text"` — attempt one write; returns number of bytes sent or `-1`. Callers must handle partial writes.
- `socket_close(result): fd` — close descriptor.
- `wait_readable(result): fd` — suspend the current USSR task until descriptor is readable.
- `wait_writable(result): fd` — suspend until descriptor is writable.

`wait_readable` and `wait_writable` are one-shot waits. They register the descriptor with the event queue and suspend the current scheduled process; readiness wakes it with integer zero. The event queue does not take ownership of descriptors.

## Echo server smoke test

Run `tests/socket_echo.su` using the freshly rebuilt interpreter, then connect
from another terminal:

```sh
printf 'hello-ukraine' | nc 127.0.0.1 18991
```

The example binds to port 18991, waits for a client, waits for request bytes,
reads up to 1024 bytes, writes them back, and closes the connection.

## Scope and limitations

- POSIX/Linux implementation only. Windows socket support is not implemented yet.
- IPv4 only; no DNS names, IPv6, TLS, HTTP parsing, or HTTP framework yet.
- Socket descriptors are plain integer values. Keep track of ownership and close them.
- `socket_recv` returns text, not arbitrary binary data; it cannot preserve embedded NUL bytes.
- `socket_send` may send fewer bytes than requested; HTTP code must loop and wait for writability as needed.
- `socket_accept` is nonblocking. Call `wait_readable(listener)` before accepting to avoid a busy loop.
- Do not use this prototype on untrusted networks. The HTTP parser, size limits, timeouts, and robust error handling are future milestones.
