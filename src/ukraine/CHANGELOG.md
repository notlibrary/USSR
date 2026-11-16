# Ukraine changelog

## 0.2.0

Refine pass 1 from REFINE.md (builds on the library's 0.2.0 helpers).

### Core (`ukraine.su`)
- Keep-alive with pipelining (`uk_keepalive`, `uk_max_requests`):
  several requests per connection, answered in order; HTTP/1.0 and
  `Connection: close` close as they should. Off by default, so existing
  applications behave as before.
- Chunked request bodies are decoded (the handler sees a plain body with
  a Content-Length); `Expect: 100-continue` gets its interim 100.
- Strict parsing: a bad request line or header line, duplicate
  Content-Length, or Content-Length with Transfer-Encoding (request
  smuggling forms) get 400 (`uk_lenient` turns the last two off).
- Timeouts (`uk_header_timeout`, `uk_body_timeout`, `uk_idle_timeout`):
  408 when a request stalls; `uk_max_connections` answers 503 beyond the
  limit.
- Every response gets Date, Server (`uk_server_name`) and X-Request-Id
  unless the handler set them. New helpers: `uk_read_one`,
  `uk_recv_until`, `uk_dechunk`, `uk_check_head`, `uk_header_count`,
  `uk_http_date`, `uk_request_id`, `uk_finish_response`,
  `uk_status_response`.

### Framework (`app.su`)
- Applications as btrees: routes with `:params` and `*` wildcards, 404,
  405 with Allow, HEAD from GET routes, OPTIONS answered, TRACE/CONNECT
  refused; mounting under prefixes, virtual hosts, per-path body limits,
  error pages per status, middleware before and hooks after.
- Requests and responses as btrees (`uk_parse_request`, `uk_res`,
  `uk_text`, `uk_html`, `uk_json`, `uk_redirect`, `uk_res_text`);
  old-style handlers that return raw text still work.
- Query strings, urlencoded and multipart forms with uploads, cookies
  (parse, and set with Secure/HttpOnly/SameSite/Max-Age), JSON bodies.
- HMAC-signed values and session cookies, CSRF tokens, Basic and Bearer
  authentication, HS256 JWT verification.
- Static files: MIME types, index.html, ETag and 304, Range (206/416),
  Cache-Control, path traversal refused.
- Middleware: CORS with preflight, rate limiting, Combined Log Format
  access log, security headers (CSP and friends).
- Content negotiation, `uk_error` (JSON or text), `uk_sse`, `uk_render`
  (HTML templates escaped by default), `uk_client_ip` for trusted
  proxies.
- `uk_app_call` runs a request through an application without a socket,
  for tests.

### Tests
- `tests/ukraine_app_test.su` (60 checks, offline, in run_all; ussr and
  russia.su) and `tests/ukraine_keepalive.su` (12 checks on port 18995,
  by hand).
- russia.su: `async`/`process`/`load` with the function name in a
  variable now spawn as in ussr (the framework calls handlers that way).

## 0.1.0

- HTTP/1.1 server with one route function per application, one request
  per connection, Content-Length bodies up to `uk_max_body`, and HTTPS
  through Ryazan.
