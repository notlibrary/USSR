# Dubna

Dubna is a PostgreSQL client written in USSR: the D of the **ROUND stack**.

| Layer | Name | Job |
|-------|------|-----|
| R | [Ryazan](../ryazan/) | TLS |
| U | [Ukraine](../ukraine/) | HTTP server |
| N | [Novgorod](../novgorod/) | the forum (`--db` keeps its posts in PostgreSQL) |
| D | Dubna | PostgreSQL protocol 3.0 client, and Dubna KV, a store of its own (`--kvdb`) |

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

## Dubna KV: a store in a file

Dubna also keeps a database of its own, no server needed: Dubna KV
(`src/dubna/kv.su`, included by `dubna.su`), a key-value store in one
file in the manner of LMDB. Its pages make a copy-on-write B+ tree;
in memory the pages and their entries are the language's `btree`
container, the one the Belarus file system is built on.

```USSR
$include "src/dubna/dubna.su"

init(ret): argc argv [
    dubna_kv_open(db): "forum.kv"
    dubna_kv_put(_): db "post/0000000001" "first!"
    dubna_kv_put(_): db "seq" 2
    dubna_kv_commit(_): db                  # durable from here
    dubna_kv_range(posts): db "post/" "post0" -1
    print(_): posts                         # [[`post/0000000001`,`first!`]]
    set(ret): 0
]
```

| Call | Does |
|------|------|
| `dubna_kv_open(db): path` | opens the store in file `path`, making it; null and `dubna_kv_error` on failure |
| `dubna_kv_get(v): db key` | the value, or null |
| `dubna_kv_put(r): db key value` | stores it: 0, or -1 |
| `dubna_kv_del(b): db key` | removes it: true when it was there |
| `dubna_kv_range(v): db lo hi max` | `[key, value]` pairs with `lo <= key < hi`, in order, at most `max` (-1: all); null bounds are open |
| `dubna_kv_count(n): db` | the number of keys |
| `dubna_kv_commit(r): db` | makes the changes since the last commit durable: 0, or -1 |
| `dubna_kv_abort(r): db` | forgets them |
| `dubna_kv_close(r): db` | done with it (what is not committed is lost) |
| `dubna_kv_info(v): db` | `[txn, pages, depth, free pages]` |
| `dubna_kv_error(m):` | why the last call failed |

Keys are numbers or strings, in the `btree` order: numbers first, then
strings byte by byte (a string key holds no backquote or line end and
is under 200 bytes: pad numbers in string keys, `post/0000000042`, so
they sort as numbers). Values are strings (anything but byte 0),
numbers, booleans, vectors and structs.

**The file.** Pages of 4096 bytes. Pages 0 and 1 are meta pages, `[txn,
root, pages, free list, count]`; the others are B+ tree nodes, `["L",
[[key, value], ...]]` leaves and `["B", [[key, child], ...]]` branches,
or runs of data pages holding one big value (a string with line ends or
backquotes, or anything over 1000 bytes of UNO). Every page starts with
an 8-byte magic (`DKVMETA1`, `DKVNODE1`, `DKVDATA1`) and the length and
CRC-32 of its text in hex; the text follows, padded with spaces, so a
hex viewer shows the whole store.

**Commits.** A change never writes a page the last commit can reach:
the path from the root to the changed leaf is copied to fresh pages
(from the free list, else the end of the file). `dubna_kv_commit`
writes them, then the meta page that held the older commit (`txn % 2`).
Opening takes the valid meta with the highest txn, so a crash, or a
torn meta page, loses at most the commit being written. A page freed by
commit n is used again from commit n + 2: by then neither meta can
reach it, and the file stops growing.

**Memory mapped, the USSR way.** USSR has no mmap. A page is read with
`bread` the first time a lookup comes to it and stays in a page cache
(a `btree` from page number to node) for as long as the store is open,
the way a mapped page stays in memory; changed pages wait there until
the commit writes them with `bwrite`. On Windows and Linux the file is
the host's; in Moscow it is a file of the Belarus root, which `bread`
and `bwrite` reach inside (`bl_pread`, `bl_pwrite`).

One process and one writer: a store is opened once and its handle
shared by the tasks of the program, whose calls never interleave.

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

Tests: `ussr tests/dubna_test.su` and `ussr tests/dubna_kv_test.su` run
without a server (part of `tests/run_all.sh`); `DUBNA_TEST_URL=... ussr tests/dubna_live.su` and
`tests/novgorod_db_live.su` talk to a real one. See
[tests/TESTPLAN.md](tests/TESTPLAN.md) and [docs/API.md](docs/API.md).
