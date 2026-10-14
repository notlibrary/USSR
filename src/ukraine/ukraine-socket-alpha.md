# ukraine.su HTTP server

`ukraine.su` is a small HTTP/1.1 server and request-handler framework written
in USSR. Its nonblocking TCP sockets are provided by the native socket library;
connection handlers are ordinary USSR processes scheduled with `async`. The
server does not fork or exec a process per connection.

## Run

Build USSR with the normal platform build, then from the repository root run:

```text
ussr tests/ukraine_http.su
```

The example listens on `127.0.0.1:18992` and provides `/`, `/health`, and a
404 fallback. Use `curl -i http://127.0.0.1:18992/health` to try it.

## Application API

Include `src/ukraine/ukraine.su` and define the route hook:

```USSR
uk_route(result): method target raw_request [
    if(_): {method = "GET"} [
        if(_): {target = "/"} [
            uk_response(result): "200 OK" "text/plain; charset=utf-8" "hello\n"
            return(result): result
        ]
    ]
    uk_response(result): "404 Not Found" "text/plain; charset=utf-8" "Not Found\n"
]
```

The hook receives the method, request target, and raw request string. Build a
response with `uk_response(result): status_line content_type body`, where
`status_line` includes the status code and reason phrase (for example,
`"200 OK"`). Start the listener with `uk_server(result): host port`.
`uk_server` accepts a connection and schedules `uk_client` to handle it while
the main task continues accepting connections.

## Socket functions

The `socket_*` commands use small integer handles rather than native OS
descriptors:

- `socket_tcp(handle):` creates a nonblocking IPv4 TCP socket.
- `socket_bind(result): handle ipv4_address port` and
  `socket_listen(result): handle backlog` prepare a listener.
- `socket_accept(client): handle` accepts a ready connection.
- `socket_nonblocking(result): handle` configures a socket as nonblocking.
- `socket_recv(data): handle max_bytes` reads text; `socket_send(sent): handle
  text` may perform a partial write.
- `socket_close(result): handle` closes a socket.
- `wait_readable(result): handle` and `wait_writable(result): handle` suspend
  the current scheduler process until readiness.

The POSIX backend uses `poll`; Windows uses Winsock `WSAPoll`. Build inputs are
selected by the platform makefiles.

The HTTP layer currently serves one request per connection, bounds request
headers to 16 KiB, and closes the connection after its response. It does not
parse request bodies or implement TLS, DNS, or IPv6. The lower-level socket
examples are `tests/socket_smoke.su` and `tests/socket_echo.su`; the HTTP
helper test is `tests/ukraine_unit.su`.
