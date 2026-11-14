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

## Applications on top
- Virtual hosts: several applications by `Host` header on one port
- A reverse proxy and load balancer handler (upstream list, health
  checks, retries)
- Mounting: several `uk_route` handlers under path prefixes (today one
  application defines the single `uk_route`)
- Background jobs and scheduled tasks beside the server
- Per-route body limits (today the global `uk_max_body`)
- File uploads streamed to disk, with a size limit and a progress
  callback
- Request and response hooks for tests: call a route without a socket
  and inspect the result (Novgorod's test does this by hand)
- Hot reload in development: restart handlers when a `.su` file
  changes
- A project scaffold: `ukraine new app` with routes, templates, static
  files and a test

## Standards and interop
- HTTP caching headers helper (`Cache-Control`, `Vary`, `Age`)
- Content negotiation (`Accept`, `Accept-Language`,
  `Accept-Encoding`)
- CORS preflight handling with an origin list
- Basic and Bearer authentication helpers, and JWT verification
- OpenAPI description generated from the route table
- CGI/FastCGI/SCGI gateways to run other programs behind Ukraine
- `HEAD` and `OPTIONS` answered by the framework for every route
- `Date` and `Server` headers, and correct handling of `TRACE` and
  `CONNECT` refusals
- HTTP/3 (QUIC) as a long-range goal

## Operations
- Worker processes sharing a listening socket, for more than one core
- Zero-downtime restart: hand the listener to a new process
- systemd socket activation and notify
- A status page with live connections and recent errors
