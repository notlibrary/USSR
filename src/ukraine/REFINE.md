# Ukraine — refinement ideas

**Today:** a small HTTP/1.1 server and handler framework (~350 lines).
There is one `uk_route` per application, one request per connection
(closed after the response), and IPv4 TCP. The request-header limit is
16 KiB, bodies are read by Content-Length up to `uk_max_body`, and a
longer body gets 413. Helpers: `uk_request_header`, `uk_request_body`,
`uk_response` and `uk_response_ex`. HTTPS goes through Ryazan
(`uk_server_tls`). Each connection is a scheduler task. Its own README
says to keep it on trusted networks until timeouts and limits exist.

## Protocol
- Keep-alive: several requests per connection, plus pipelining
- Chunked request body decoding, and chunked or streaming responses
- `Expect: 100-continue`
- HTTP/1.0 compatibility details and `Connection: close` handling
- Range requests (`206 Partial Content`) for static files and media
- Conditional requests: ETag, Last-Modified, `304 Not Modified`
- gzip/deflate response compression
- HTTP/2 (with ALPN through Ryazan)
- WebSocket upgrade (`101 Switching Protocols`) and frames
- Server-Sent Events helper (`text/event-stream`)
- IPv6 listening and dual-stack
- Unix domain sockets behind a reverse proxy
- `PROXY` protocol, and `X-Forwarded-For` / `Forwarded` trust settings

## Hardening
- Timeouts: header read, body read, idle keep-alive, and total request
  time (slowloris)
- A connection limit, plus per-IP connection and request limits
- Strict request-line and header parsing, refusing
  request-smuggling forms (CL+TE, duplicate Content-Length)
- Security headers helper (CSP, X-Content-Type-Options, Referrer-Policy)
- Graceful shutdown: stop accepting, finish in-flight requests
- Request IDs and structured error responses

## Framework
- A router: method + path patterns (`/thread/:id`, wildcards), 405 with
  `Allow`
- Middleware chain: logging, auth, CORS, rate limiting, compression
- Query string and form parsing (`application/x-www-form-urlencoded`,
  multipart; Novgorod has its own and could share it)
- Cookies: parse and set with flags (Secure, HttpOnly, SameSite)
- Sessions (signed cookie), plus CSRF tokens
- JSON request/response helpers
- Static file serving with MIME types, directory index, caching headers
  and path traversal protection
- Templates: a small HTML template engine with escaping by default
- Response struct (status, headers vector, body) in place of string
  assembly
- Binary-safe bodies (byte vectors) for images and downloads
- Error pages per status

## Observability
- Access log in Common/Combined Log Format
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
