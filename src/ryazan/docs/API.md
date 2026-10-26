# Ryazan API

## Script library (`ryazan.su`)

`$include "src/ryazan/ryazan.su"` (relative to the including file). The
waiting functions call `wait_readable`/`wait_writable`, so they run inside a
scheduler process: `init(...)` or an `async` task.

| Call | Result |
|------|--------|
| `ryazan_connect(s): host port` | connected TLS client socket, or `-1` |
| `ryazan_accept(r): socket cert secret` | `0` after a server handshake on an accepted socket, else `-1` |
| `ryazan_handshake(r): socket` | drives a started handshake to the end: `0` or `-1` |
| `ryazan_read(t): socket max` | up to `max` bytes; `""` once the peer closed (or on error) |
| `ryazan_read_all(t): socket` | everything until the peer closes |
| `ryazan_write(r): socket text` | `0` when all of `text` is sent, else `-1` |
| `ryazan_close(r): socket` | sends close_notify and closes the socket |
| `ryazan_request(resp): method url headers body` | raw response, or `null` |
| `ryazan_get(body): url` | body of a 2xx response, or `null` |
| `ryazan_post(body): url content_type data` | body of a 2xx response, or `null` |
| `ryazan_status(code): resp` | status code of a raw response (`0` if unparsable) |
| `ryazan_body(body): resp` | body of a raw response |
| `ryazan_header(value): resp name` | header value (name is case-insensitive), `""` if absent |
| `ryazan_error(msg):` | why the last call failed |

`headers` holds request header lines separated by `"\n"` (a trailing
newline is optional; `""` for none). `Host`, `Connection: close`,
`Content-Length`, `User-Agent` and `Accept` are added unless given. A raw
response is the status line and headers, a blank line, then the body with
chunked transfer coding already removed.

```USSR
ryazan_request(resp): "POST" "https://httpbin.org/post" "Content-Type: application/json\nX-Trace: 7" "{\"a\":1}"
ryazan_status(code): resp
ryazan_header(type): resp "content-type"
```

## Builtins

Sessions are named by their socket handle.

| Builtin | Result |
|---------|--------|
| `socket_connect(r): socket host port [timeout_ms]` | `0` or `-1`; host is an IPv4 address or a DNS name; default timeout 30000 |
| `tls_backend(b):` | `"schannel"`, `"openssl"` or `"none"` |
| `tls_client(r): socket host` | `0`/`-1`; starts a client session that verifies `host` |
| `tls_server(r): socket cert [secret]` | `0`/`-1`; starts a server session |
| `tls_handshake(r): socket` | `1` done, `0` wait until readable, `2` wait until writable, `-1` failed |
| `tls_recv(t): socket max` | text; `""` closed or failed; `null`: wait until readable, then call again |
| `tls_send(n): socket text` | bytes taken (`0`: earlier output still queued, wait until writable), `-1` failed |
| `tls_flush(r): socket` | `1` all sent, `2` wait until writable, `-1` failed |
| `tls_pending(n): socket` | decrypted bytes already buffered (read these before waiting) |
| `tls_info(i): socket` | e.g. `"TLS 1.3, TLS_AES_256_GCM_SHA384"`; `""` before the handshake |
| `tls_close(r): socket` | sends close_notify and drops the session; the socket stays open |
| `tls_request(resp): method url [headers [body [timeout_s]]]` | raw response, or `null` (see `tls_error`) |
| `tls_error(msg):` | the last TLS/HTTP error message |

`socket_close` also drops a socket's TLS session.

## C API (`src/ryazan.h`)

The same operations for native code, plus `ryazan_http_request()`, the
blocking HTTP/1.1 client that `prompt(_):` uses. A backend implements the
small interface in `src/ryazan_backend.h`: it turns ciphertext from
`conn->in` into plaintext and plaintext into queued ciphertext, and never
touches the socket itself.
