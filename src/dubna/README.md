# Dubna

Dubna is a PostgreSQL client written in USSR: the D of the **RUND stack**.

| Layer | Name | Job |
|-------|------|-----|
| R | [Ryazan](../ryazan/) | TLS |
| U | [Ukraine](../ukraine/) | HTTP server |
| N | [Novgorod](../novgorod/) | the forum (`--db` keeps its posts in PostgreSQL) |
| D | Dubna | PostgreSQL protocol 3.0 client |

```USSR
$include "src/dubna/dubna.su"

init(ret): argc argv [
    dubna_connect(db): "postgres://forum:secret@localhost/forum?sslmode=require"
    if(_): {db = null} [
        dubna_error(why):
        print(_): why
        set(ret): 1
        return(ret): ret
    ]
    dubna_exec(res): db "select no, subject from posts where board = $1 order by no desc limit 10" '[`c`]'
    dubna_count(n): res
    set(i): 0
    while(_): {i < n} [
        dubna_value(subject): res i "subject"
        print(_): subject
        add(i): i 1
    ]
    dubna_close(_): db
    set(ret): 0
]
```

## What it speaks

- **Connecting:** a conninfo string (`host=... port=... dbname=... user=...
  password=... sslmode=...`, values may be `'quoted'`) or a URL
  (`postgres://user:pass@host:port/db?sslmode=...`, percent-escapes allowed).
  Missing settings come from `PGHOST`, `PGPORT`, `PGDATABASE`, `PGUSER`,
  `PGPASSWORD`, `PGSSLMODE`, then `localhost`, `5432` and your user name,
  like libpq.
- **TLS** through Ryazan (Schannel or OpenSSL). `sslmode=prefer` (the
  default) uses TLS when the server offers it and, like libpq, falls back
  to a plain connection when TLS fails; `require` insists, `disable` never
  asks. Ryazan always checks the certificate chain and the host name,
  so `require` behaves like libpq's `verify-full`; set `RYAZAN_CA_FILE` to
  trust a private CA or a self-signed certificate.
- **Logins:** trust, cleartext password, md5 and SCRAM-SHA-256, the default
  since PostgreSQL 14. Dubna also checks the server's SCRAM signature, so a
  server that does not know the password cannot pretend it does.
- **Queries:** `dubna_query` sends the simple protocol (several statements
  are allowed; the last result is kept). `dubna_exec` sends one statement
  through Parse/Bind/Execute with `$1, $2 ...` parameters taken from a
  vector. Parameters never become SQL, so they need no quoting.
- **Results:** text values (`null` for SQL NULL), column names and type
  OIDs, the command tag, the number of rows returned or affected, and on
  error the server's message and SQLSTATE (`res.code`, e.g. `23505` for a
  unique violation). Notices are collected on the connection.

## How bytes get through

The wire protocol is binary and full of zero bytes, which USSR strings
cannot hold, so Dubna works on vectors of bytes 0..255. This needed a few
new builtins in ussr (C) and in russia.su:

| Builtin | |
|---------|-|
| `socket_send_bytes(n): s v`, `socket_recv_bytes(v): s max` | the socket API for byte vectors |
| `tls_send_bytes(n): s v`, `tls_recv_bytes(v): s max` | the same over a Ryazan TLS session |
| `bytes_text(s): v [from [count]]` | bytes to a string (`null` if one is 0) |
| `sha256`, `hmac_sha256`, `pbkdf2_sha256`, `md5` | the hashes SCRAM and md5 logins need |

The hashes authenticate a login; they encrypt nothing. Encryption is TLS's
job, and Ryazan leaves it to the operating system.

## Limits

- One connection serves one task at a time. Open one per task (logins take
  a few milliseconds), as Novgorod does for each write.
- Text results only (no binary format), no COPY, no LISTEN/NOTIFY
  callbacks, no query cancellation, no SCRAM channel binding
  (`SCRAM-SHA-256-PLUS`), no Kerberos, GSSAPI or SSPI logins.
- A password is used as given (no SASLprep), which matters only for
  non-ASCII passwords that are not in their normal form.
- `dubna_init` declares Dubna's types and `dubna_connect` calls it. If tasks
  started with `async` may connect first, call `dubna_init(_):` in `init`
  before starting them.

Tests: `ussr tests/dubna_test.su` runs without a server (part of
`tests/run_all.sh`); `DUBNA_TEST_URL=... ussr tests/dubna_live.su` and
`tests/novgorod_db_live.su` talk to a real one. See
[tests/TESTPLAN.md](tests/TESTPLAN.md) and [docs/API.md](docs/API.md).
