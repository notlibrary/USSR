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

## Script layers (refine pass 1)

Each file includes what it needs; all run in a scheduler process.

| File | Calls |
|------|-------|
| `ryazan.su` | `ryazan_starttls(r): socket host`, `ryazan_connect_timeout(s): host port ms`, `ryazan_connect_ca(s): host port ca`, `ryazan_error_kind(k): msg`, `ryazan_explain(t): msg`, `tls_features(v):`, `ryazan_verbose(_): on`, `ryazan_info(t): socket` |
| `http.su` | `ryazan_fetch(resp): method url headers body opts`, `ryazan_fetch_error(e):`, `ryazan_resp_header(v): resp name`, `ryazan_resp_headers(v): resp name`, `ryazan_url_parse(v): url`, `ryazan_url_resolve(r): base ref`, `ryazan_form(r): pairs`, `ryazan_multipart(v): parts`, `ryazan_get_json(v): url opts`, `ryazan_post_json(v): url value opts`, `ryazan_cookies_new(j):`, `ryazan_cookies_store(_): jar url set_cookie`, `ryazan_cookies_header(r): jar url`, `ryazan_hsts_add(_): host seconds`, `ryazan_hsts_has(b): host`, `ryazan_download(r): url path opts`, `ryazan_pool_close(_):`, `ryazan_check(r): url` |
| `ws.su` | `ryazan_ws_connect(c): url headers opts`, `ryazan_ws_accept(c): socket tls`, `ryazan_ws_send(r): c text`, `ryazan_ws_recv(m): c`, `ryazan_ws_ping(r): c data`, `ryazan_ws_close(r): c code reason`, `ryazan_ws_path(p): c`, `ryazan_sha1(v): data`, `ryazan_ws_accept_key(r): key`, `ryazan_ws_frame(v): opcode data mask` |
| `smtp.su` | `ryazan_smtp_send(r): opts from to subject body`, `ryazan_smtp_error(e):`, `ryazan_smtp_message(t): from to subject body` |
| `server.su` | `ryazan_listen(s): ip port backlog`, `ryazan_serve(r): listener cert secret handler opts`, `ryazan_handshake_timed(r): socket seconds`, `ryazan_redirect_server(r): listener https_port count`, `ryazan_reload(_): cert secret` |

`ryazan_fetch` options (a btree, or `null` for the defaults): `timeout`
(seconds, whole exchange), `connect_timeout` (ms, 30000), `max_redirects`
(5), `retries` (0), `backoff` (1 s, doubling), `keepalive` (false),
`ca_file`, `proxy` (default from `HTTPS_PROXY`/`HTTP_PROXY`; `"none"`
turns it off), `cookies` (a jar), `binary`, `body_file`, `user_agent`.
The header comment of each file documents the details.

```USSR
$include "src/ryazan/http.su"
init(r): argc argv [
    btree(o):
    btput(_): o "timeout" 20
    btput(_): o "retries" 2
    ryazan_fetch(resp): "GET" "https://example.org/" "" "" o
    btget(code): resp "status"
    ryazan_resp_header(type): resp "content-type"
]
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
