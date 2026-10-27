# Dubna API

`$include "src/dubna/dubna.su"` (relative to the including file). Calls that
talk to the server wait on the socket, so they run inside a scheduler
process: `init` or an `async` task.

## Connections

| Call | Result |
|------|--------|
| `dubna_connect(c): conninfo` | a `DubnaConn`, or `null`; `dubna_error` says why |
| `dubna_close(r): c` | sends Terminate and closes the socket (also fine on `null`) |
| `dubna_error(msg):` | why the last call failed, e.g. `dubna: FATAL 28P01: password authentication failed for user "x"` |
| `dubna_parameter(v): c name` | a server setting reported at login or since (`server_version`, `TimeZone`, `client_encoding`...), `""` if unknown |
| `dubna_init(_):` | declares `DubnaConn` and `DubnaResult`; `dubna_connect` calls it |

`DubnaConn` fields worth reading: `tls` (true when encrypted), `status`
(`"I"` idle, `"T"` in a transaction, `"E"` in a failed transaction), `pid`,
`open`, `notices` (vector of notice texts).

## Queries

| Call | Result |
|------|--------|
| `dubna_query(res): c sql` | runs one or more statements (simple protocol); the last statement's result |
| `dubna_exec(res): c sql params` | one statement with `$1, $2 ...` bound from the vector `params`: strings as they are, integers and reals as decimal text, booleans as `true`/`false`, `null` as SQL NULL |

Both always return a `DubnaResult`. Check `dubna_ok` before using it.

## Results

| Call | Result |
|------|--------|
| `dubna_ok(b): res` | true when the statement succeeded |
| `dubna_count(n): res` | rows returned; for statements without rows, the count in the tag (`INSERT 0 5` gives 5) |
| `dubna_value(v): res row col` | the field as text, `null` for SQL NULL or a row/column that is not there; `col` is an index or a column name |
| `dubna_column(i): res name` | index of a column, `-1` if absent |

`DubnaResult` fields: `columns` (names), `types` (type OIDs: 23 int4, 20
int8, 25 text, 16 bool, 1700 numeric, 1114 timestamp...), `rows` (vector of
rows, each a vector of strings and nulls), `tag` (`SELECT 3`, `UPDATE 1`),
`error` (`""` on success) and `code` (the SQLSTATE on error).

## conninfo

| Setting | Environment | Default |
|---------|-------------|---------|
| `host` | `PGHOST` | `localhost` (IPv4 address or DNS name) |
| `port` | `PGPORT` | `5432` |
| `dbname` | `PGDATABASE` | the user name |
| `user` | `PGUSER` | `USERNAME` / `USER` |
| `password` | `PGPASSWORD` | none |
| `sslmode` | `PGSSLMODE` | `prefer` (`disable`, `allow`, `prefer`, `require`, `verify-ca`, `verify-full`; every TLS connection verifies the certificate; `prefer` reconnects without TLS when TLS fails, the others give up) |

## Building blocks

Exposed for tests and for anyone extending the driver: `dubna_conninfo`,
`dubna_setting`, `dubna_startup_message`, `dubna_ssl_request`,
`dubna_query_message`, `dubna_exec_messages`, `dubna_md5_password`,
`dubna_scram_first`, `dubna_scram_final`, `dubna_error_text`,
`dubna_row_description`, `dubna_data_row`, `dubna_base64`,
`dubna_unbase64`, `dubna_hex`, `dubna_put_int32`, `dubna_put_int16`,
`dubna_put_cstr`, `dubna_int32`, `dubna_int16`.

## New builtins

| Builtin | Result |
|---------|--------|
| `socket_send_bytes(n): socket v` | sends a vector of bytes: count sent, `-1` on error |
| `socket_recv_bytes(v): socket max` | up to `max` (1..65536) bytes as a vector; empty once the peer closed, `null` on error |
| `tls_send_bytes(n): socket v` | `tls_send` for a byte vector (`0`: wait until writable) |
| `tls_recv_bytes(v): socket max` | `tls_recv` as a byte vector: empty once closed or failed, `null`: `wait_readable` and call again |
| `bytes_text(s): v [from [count]]` | the bytes as a string, `null` if one of them is 0 |
| `sha256(v): data` | SHA-256 digest, 32 bytes |
| `md5(v): data` | MD5 digest, 16 bytes (old protocols only; not for security) |
| `hmac_sha256(v): key data` | HMAC-SHA-256, 32 bytes |
| `pbkdf2_sha256(v): password salt iterations [length]` | PBKDF2-HMAC-SHA-256, 32 bytes unless `length` (1..1024) says otherwise |

Hash inputs are strings or byte vectors. Results are byte vectors.
