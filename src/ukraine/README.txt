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

See `tests/ukraine_http.su` for a runnable example and
`tests/ukraine_unit.su` for helper checks. The existing
`tests/socket_smoke.su` and `tests/socket_echo.su` exercise the lower-level
socket API.

The socket API returns small integer handles, not native descriptors. The
current HTTP layer is intentionally small: IPv4 TCP, one request per
connection, a 16 KiB request-header limit, and application-defined routing.
It does not yet parse request bodies, implement TLS, or provide DNS/IPv6.
Keep the server on trusted networks until those features and production-grade
timeouts/limits are added.
