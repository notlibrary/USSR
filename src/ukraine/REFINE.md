# Ukraine — refinement ideas

**Today:** a small HTTP/1.1 server and handler framework (~350 lines).
There is one `uk_route` per application, one request per connection
(closed after the response), and IPv4 TCP. The request-header limit is
16 KiB, bodies are read by Content-Length up to `uk_max_body`, and a
longer body gets 413. Helpers: `uk_request_header`, `uk_request_body`,
`uk_response` and `uk_response_ex`. HTTPS goes through Ryazan
(`uk_server_tls`). Each connection is a scheduler task. Its own README
says to keep it on trusted networks until timeouts and limits exist.

**Refine pass 1 (2026-11-16):** [x] done, [~] partly done; see
CHANGELOG.md 0.2.0. The core (ukraine.su) gained keep-alive, chunked
bodies, timeouts and strict parsing behind opt-in globals; app.su is
the new framework layer.

## Protocol
- [x] Keep-alive: several requests per connection, plus pipelining
- [~] Chunked request body decoding, and chunked or streaming responses — partly: chunked request bodies are decoded; responses are still whole (uk_sse formats event-stream messages)
- [x] `Expect: 100-continue`
- [x] HTTP/1.0 compatibility details and `Connection: close` handling
- [x] Range requests (`206 Partial Content`) for static files and media
- [x] Conditional requests: ETag, Last-Modified, `304 Not Modified`
- gzip/deflate response compression
- HTTP/2 (with ALPN through Ryazan)
- WebSocket upgrade (`101 Switching Protocols`) and frames
- [~] Server-Sent Events helper (`text/event-stream`) — partly: uk_sse formats messages; a streaming response is not possible yet, since handlers return whole responses
- IPv6 listening and dual-stack
- Unix domain sockets behind a reverse proxy
- [~] `PROXY` protocol, and `X-Forwarded-For` / `Forwarded` trust settings — partly: uk_client_ip reads X-Forwarded-For / Forwarded when the app says the proxy is trusted; no PROXY protocol

## Hardening
- [~] Timeouts: header read, body read, idle keep-alive, and total request
  time (slowloris) — partly: header, body and idle keep-alive timeouts (globals); no total request time
- [~] A connection limit, plus per-IP connection and request limits — partly: uk_max_connections (503 beyond); per-client request limits through uk_mw_rate_limit; no per-IP connection limit (the socket API gives no peer address)
- [x] Strict request-line and header parsing, refusing
  request-smuggling forms (CL+TE, duplicate Content-Length)
- [x] Security headers helper (CSP, X-Content-Type-Options, Referrer-Policy)
- Graceful shutdown: stop accepting, finish in-flight requests
- [~] Request IDs and structured error responses — partly: X-Request-Id on every response and uk_error (JSON or text by Accept)

## Framework
- [x] A router: method + path patterns (`/thread/:id`, wildcards), 405 with
  `Allow`
- [~] Middleware chain: logging, auth, CORS, rate limiting, compression — partly: uk_app_use / uk_app_after with CORS, rate limiting, logging and security headers ready to use; no compression
- [~] Query string and form parsing (`application/x-www-form-urlencoded`,
  multipart; Novgorod has its own and could share it) — partly: urlencoded and multipart (fields and uploads) through uk_form, uk_form_fields and uk_upload
- [x] Cookies: parse and set with flags (Secure, HttpOnly, SameSite)
- [x] Sessions (signed cookie), plus CSRF tokens
- [x] JSON request/response helpers
- [~] Static file serving with MIME types, directory index, caching headers
  and path traversal protection — partly: MIME types, directory index with a slash redirect, ETag/304, Range, Cache-Control, traversal refusal; no Last-Modified (no file time call)
- [x] Templates: a small HTML template engine with escaping by default
- [x] Response struct (status, headers vector, body) in place of string
  assembly
- Binary-safe bodies (byte vectors) for images and downloads
- [x] Error pages per status

## Observability
- [x] Access log in Common/Combined Log Format
- Metrics: request counts, latencies and status codes, at a `/metrics`
  endpoint
- Debug mode with request dumps

## Client
- A matching HTTP client (`uk_client` exists) with keep-alive and
  timeouts, sharing parsing code with Ryazan

## Tests
- Conformance tests (h1spec-style) for request parsing
- Load tests with many concurrent clients through the scheduler
- Fuzz request parsing
- Add the HTTP tests to CI with port discovery

## Applications on top
- [x] Virtual hosts: several applications by `Host` header on one port
- A reverse proxy and load balancer handler (upstream list, health
  checks, retries)
- [x] Mounting: several `uk_route` handlers under path prefixes (today one
  application defines the single `uk_route`)
- Background jobs and scheduled tasks beside the server
- [x] Per-route body limits (today the global `uk_max_body`)
- File uploads streamed to disk, with a size limit and a progress
  callback
- [x] Request and response hooks for tests: call a route without a socket
  and inspect the result (Novgorod's test does this by hand)
- Hot reload in development: restart handlers when a `.su` file
  changes
- A project scaffold: `ukraine new app` with routes, templates, static
  files and a test

## Standards and interop
- [~] HTTP caching headers helper (`Cache-Control`, `Vary`, `Age`) — partly: uk_cache for Cache-Control; Vary set by the CORS middleware; no Age
- [~] Content negotiation (`Accept`, `Accept-Language`,
  `Accept-Encoding`) — partly: Accept (uk_accepts, uk_best_type); not Accept-Language or Accept-Encoding
- [x] CORS preflight handling with an origin list
- [~] Basic and Bearer authentication helpers, and JWT verification — partly: Basic and Bearer helpers and HS256 JWT verification (exp checked)
- OpenAPI description generated from the route table
- CGI/FastCGI/SCGI gateways to run other programs behind Ukraine
- [x] `HEAD` and `OPTIONS` answered by the framework for every route
- [x] `Date` and `Server` headers, and correct handling of `TRACE` and
  `CONNECT` refusals
- HTTP/3 (QUIC) as a long-range goal

## Operations
- Worker processes sharing a listening socket, for more than one core
- Zero-downtime restart: hand the listener to a new process
- systemd socket activation and notify
- A status page with live connections and recent errors
