# Changelog

## 0.2.0
Refine pass 1 from REFINE.md; the new calls are in `more.su` (include it
in place of `dubna.su`; it includes Dubna and the library helpers).
- Prepared statements by name (`dubna_prepare`, `dubna_exec_prepared`,
  `dubna_deallocate`) and pipelining (`dubna_pipeline`: many Bind/Execute
  pairs before one Sync).
- `dubna_query_all` keeps every result of a multi-statement query.
- Typed rows: `dubna_rows`, `dubna_records`, `dubna_columns`,
  `dubna_typed` (integers, reals, booleans, json/jsonb, arrays; numeric
  stays exact text) and `dubna_array`.
- `COPY FROM STDIN` / `TO STDOUT` (`dubna_copy_in`, `dubna_copy_out`).
- `LISTEN` / `NOTIFY` with `dubna_wait_notify(c ms)`.
- SQL cursors for large results (`dubna_cursor`, `dubna_fetch`,
  `dubna_cursor_close`).
- `dubna_cancel`: a CancelRequest over a second socket (dubna.su now
  remembers each connection's server address).
- Transactions: begin/commit/rollback, savepoints, and `dubna_tx(c fn)`
  that commits when fn gives 0 and rolls back otherwise.
- `dubna_quote_ident`, `dubna_quote_literal`, `dubna_set`,
  `dubna_server_version`, `dubna_notices`.
- `dubna_connect_retry` with doubling backoff, `.pgpass` support
  (`dubna_pgpass`, `dubna_connect_pgpass`), and a connection pool that
  checks idle connections with `SELECT 1`.
- `dubna_migrate(c dir)`: numbered `.sql` files recorded in
  `schema_migrations`, each in a transaction; `dubna_explain`.
- Dubna KV: `dubna_kv_prefix`, `dubna_kv_range_desc`, TTL keys
  (`dubna_kv_put_ttl`, `dubna_kv_get_ttl`), JSON-lines
  `dubna_kv_export` / `dubna_kv_import`.
- A Redis client: RESP encode/decode and `dubna_redis_connect`,
  `dubna_redis`, `dubna_redis_close`.
- Tests: `tests/dubna_more_test.su` (28 offline checks, ussr and
  russia.su) and `tests/dubna_more_live.su` (45 checks against a
  PostgreSQL 18 server); the existing Dubna tests are unchanged.

## 0.1.0
- First version: a PostgreSQL protocol 3.0 client in USSR. conninfo strings
  and URLs with libpq's environment variables; TLS through Ryazan
  (`sslmode` disable/prefer/require, certificate always verified); trust,
  password, md5 and SCRAM-SHA-256 logins with the server signature checked;
  simple queries and parameterised Parse/Bind/Execute; text results with
  NULLs, column names, types, tags, counts, errors with SQLSTATE, notices.
- New builtins for it in ussr and russia.su: `socket_send_bytes`,
  `socket_recv_bytes`, `tls_send_bytes`, `tls_recv_bytes`, `bytes_text`,
  `sha256`, `md5`, `hmac_sha256`, `pbkdf2_sha256`.
- Novgorod's `--db` keeps forum posts in PostgreSQL through Dubna.
