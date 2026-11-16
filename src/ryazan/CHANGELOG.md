# Ryazan changelog

## 0.2.0

Refine pass 1 from REFINE.md (builds on the library's 0.2.0 helpers).

- `http.su`: a script HTTP/1.1 client, `ryazan_fetch(resp): method url
  headers body opts`, that yields to the scheduler while it waits.
  Responses are btrees (status, reason, version, headers as
  `[name, value]` pairs, body, url, redirects, attempts, complete).
  Options per request: total timeout and connect timeout, redirect
  following with a limit (POST becomes GET on 301/302/303; Authorization
  and Cookie are dropped across origins), retries with backoff on
  failures, 429 and 503 (Retry-After honoured), a keep-alive connection
  pool, a per-request CA file, proxies (`HTTPS_PROXY`/`HTTP_PROXY`,
  CONNECT for https, the absolute form for http), a cookie jar, binary
  bodies, chunked uploads from a vector or streamed from a file, and the
  User-Agent. HSTS answers are remembered and later `http://` requests to
  those hosts go out as `https://`.
- Helpers: `ryazan_url_parse`, `ryazan_url_resolve`, `ryazan_resp_header`
  / `ryazan_resp_headers`, `ryazan_form`, `ryazan_multipart`,
  `ryazan_get_json` / `ryazan_post_json`, the cookie jar
  (`ryazan_cookies_new/store/header`), `ryazan_hsts_add/has`,
  `ryazan_download` (binary-safe, SHA-256 check), `ryazan_pool_close` and
  `ryazan_check(url)`.
- `ws.su`: WebSocket clients and servers (ws:// and wss://): handshake
  with a pure-USSR SHA-1, masked client frames, 16- and 64-bit lengths,
  fragments, ping/pong and close. Frames travel as byte vectors.
- `smtp.su`: `ryazan_smtp_send` with STARTTLS (or implicit TLS), AUTH
  PLAIN only over TLS, several recipients, dot-stuffing, and
  `ryazan_smtp_message`.
- `server.su`: `ryazan_listen`, `ryazan_serve` (accept loop, TLS
  handshake with a timeout, a handler task per connection, a cap on
  concurrent tasks, a plain mode), `ryazan_handshake_timed`,
  `ryazan_redirect_server` (HTTP to HTTPS) and `ryazan_reload` for a
  renewed certificate without a restart.
- `ryazan.su`: `ryazan_starttls` (upgrade a plain socket),
  `ryazan_connect_timeout`, `ryazan_connect_ca` (per-call trust),
  `ryazan_error_kind` / `ryazan_explain` (expired, wrong host, untrusted,
  protocol, refused, DNS, timeout, closed), `tls_features`,
  `ryazan_verbose` / `RYAZAN_VERBOSE=1` and `ryazan_info`.
- Tests: `tests/ryazan_http.su` (34 checks) and `tests/ryazan_services.su`
  (19) on loopback ports, by hand like the other port tests;
  `tests/rzh_unit.su` (38, offline) runs in `tests/run_all.sh` under ussr
  and russia.su.

## 0.1.0

- TLS clients and servers on USSR socket handles: Schannel on Windows,
  OpenSSL elsewhere. Certificate chain and host name checking, with
  `RYAZAN_CA_FILE` for private trust anchors. TLS 1.2 and 1.3.
- Builtins `socket_connect` (with DNS names) and `tls_*`, including the
  blocking HTTP/1.1 client `tls_request` for `http://` and `https://`.
- `ryazan.su` script library: connect/accept/read/write/close helpers, plus
  `ryazan_request`, `ryazan_get` and `ryazan_post` with response helpers.
- ukraine.su serves HTTPS through `uk_server_tls`; `prompt(_):` talks to AI
  providers through ryazan instead of the curl program.
