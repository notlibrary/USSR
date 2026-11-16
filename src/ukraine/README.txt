ukraine.su is a small HTTP/1.1 server and request-handler framework written in
USSR. It uses the existing nonblocking socket library and USSR's built-in
cooperative scheduler. It does not fork or exec processes to serve requests.

## Run the example

Build the interpreter with the repository's platform build:

  POSIX:   make -f ussr-ubuntu.mk ussr
  Windows: nmake /f Makefile.msc

From the repository root, run:

  ussr tests/ukraine_http.su

The example binds to 127.0.0.1:18992. In another terminal, request:

  curl -i http://127.0.0.1:18992/
  curl -i http://127.0.0.1:18992/health
  curl -i http://127.0.0.1:18992/missing

## Write an application

Include `src/ukraine/ukraine.su` and define `uk_route(result): method target
raw_request [...]`. The route returns a response by calling
`uk_response(result): "200 OK" content_type body`; status_line is the HTTP
status code and reason phrase, for example `"404 Not Found"`.
`uk_server(result): host port` creates the listener and starts one USSR `async`
scheduler task for each accepted connection. Each response closes its
connection; handlers can run concurrently while the scheduler waits for socket
readiness.

For HTTPS, call `uk_server_tls(result): host port certificate secret`
instead: each connection is wrapped in TLS by ryazan.su (src/ryazan/), with
the certificate given as a PKCS#12 file and its password (OpenSSL builds
also take a PEM certificate chain plus the PEM key file). Clients that do
not speak TLS are dropped. `tests/ukraine_https.su` serves the test
certificate in tests/ryazan/ on port 18996:

  curl --cacert tests/ryazan/localhost.cert.pem https://localhost:18996/

Requests with a Content-Length are read with their body, up to the global
`uk_max_body` (1 MiB unless the application sets it before starting the
server); a longer one is answered 413. `uk_request_header(v): request name`
returns a header (name case-insensitive, "" when absent),
`uk_request_body(b): request` the body, and
`uk_response_ex(result): status content_type headers body` adds header
lines separated by "\n" (for example "Location: /\nCache-Control: no-store").
Responses carry Content-Length. src/novgorod/ is a full application.

See `tests/ukraine_http.su` for a runnable example and
`tests/ukraine_unit.su` for helper checks. The existing
`tests/socket_smoke.su` and `tests/socket_echo.su` exercise the lower-level
socket API.

The socket API returns small integer handles, not native descriptors. The
core stays small: IPv4 TCP, a 16 KiB request-header limit, and
application-defined routing; TLS comes from ryazan. Chunked request bodies
are decoded, and requests with smuggling forms (two Content-Lengths, or
Content-Length with Transfer-Encoding) get 400.

## Settings

Set these globals before uk_server (all optional):

  uk_keepalive        true: several requests per connection, pipelining
                      answered in order (default false: one per connection)
  uk_max_requests     requests per kept-alive connection (100)
  uk_header_timeout   seconds for a request head, 0 for none (0)
  uk_body_timeout     seconds for a body after its head (0)
  uk_idle_timeout     seconds an idle kept-alive connection may wait (5)
  uk_max_connections  connections at once, 503 beyond; 0 for no limit (0)
  uk_server_name      the Server header ("ukraine"; "" sends none)

On an untrusted network set the timeouts and a connection limit.

## The framework (app.su)

`$include "src/ukraine/app.su"` for routes with parameters, middleware,
cookies, signed sessions, CSRF tokens, forms and uploads, JSON, static
files with ETag and Range, CORS, rate limits, an access log, Basic, Bearer
and JWT authentication, and `uk_app_call` to test routes without a socket.
The header comment of app.su is its reference; tests/ukraine_app_test.su
shows every part in use.
Keep the server on trusted networks until those features and production-grade
timeouts/limits are added.
