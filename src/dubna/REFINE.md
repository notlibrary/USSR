# Dubna — refinement ideas

**Today:** a PostgreSQL protocol 3.0 client in USSR. It has conninfo
strings and URLs with the PG* environment variables, TLS through Ryazan
(`disable` / `prefer` / `require`, always verify-full), and trust,
password, md5 and SCRAM-SHA-256 logins. It runs simple queries and
parameterised Parse/Bind/Execute with text results, NULLs, SQLSTATE and
notices. It also contains **Dubna KV** (`kv.su`): an LMDB-style
copy-on-write B+ tree in one file, with get/put/del/range/commit/abort.

## PostgreSQL protocol
- Prepared statements kept by name: `dubna_prepare` and
  `dubna_exec_prepared` (Parse once, Bind many)
- Binary result format for numbers, `bytea`, timestamps and UUIDs, plus
  typed decoding of text results by OID (int, float, bool, numeric,
  json, arrays)
- `COPY FROM STDIN` / `COPY TO STDOUT` for bulk loading and export
- `LISTEN` / `NOTIFY` with a `dubna_wait_notify(n): c timeout` call
- Row-by-row streaming of large results (`Execute` with a row limit and
  portals), so 1M rows need not sit in memory
- Pipelining: many Bind/Execute pairs before one Sync
- Cancelling a running query (CancelRequest on a second connection)
- Query timeouts and `statement_timeout` from conninfo
- `sslmode=verify-ca` and `verify-full` spelled out, and client
  certificates (`sslcert`, `sslkey`)
- `target_session_attrs` and several hosts in conninfo for failover
- `.pgpass` and `PGSERVICE` / `pg_service.conf`
- GSSAPI or SCRAM-SHA-256-PLUS (channel binding)
- `application_name`, `options`, and `search_path` from conninfo

## API ergonomics
- `dubna_rows(v): res`: all rows as vectors, or as structs keyed by
  column name
- `dubna_columns(v): res`: names, type OIDs and modifiers
- Transaction helpers: `dubna_begin`, `dubna_commit`, `dubna_rollback`,
  savepoints, and a `dubna_tx(r): c "fn"` wrapper that rolls back on
  failure
- Identifier and literal quoting helpers for dynamic DDL
- A connection pool for Novgorod and other servers (`dubna_pool_get`,
  `dubna_pool_put`) with health checks
- Async variants that yield to the scheduler while waiting on the
  socket, so one task's query does not block others
- Automatic reconnect with backoff

## Tooling
- `dubna.su` as a small `psql`-like REPL: `\d`, `\dt`, `\x`, `\timing`,
  history
- A migration runner: numbered `.sql` files and a `schema_migrations`
  table
- EXPLAIN pretty-printing

## Dubna KV
- Several readers with one writer, and file locking between processes
- Named sub-databases (DBIs) in one file, like LMDB
- Cursors: `dubna_kv_cursor`, `next`, `prev`, `seek`, so large ranges can
  be walked without vectors
- Reverse ranges and prefix scans (`dubna_kv_prefix`)
- Compaction or `copy --compact` to give freed pages back to the OS
- An `fsync` policy option (durable, or fast with no sync)
- A page checksum check and a `dubna_kv_verify` / repair tool
- Secondary indexes kept in step with the main tree
- TTL keys that expire after a time
- Compression of large values
- A binary page format option, faster than UNO text, with a hex-dump
  debug mode
- Snapshot export and import to and from UNO or JSON lines
- Concurrency tests that kill the process mid-commit, plus fuzzing of
  pages

## Tests
- A CI job against a containerised PostgreSQL, so `dubna_live.su` runs
  automatically
- Protocol fuzzing with recorded server replies
- Benchmarks: rows per second for simple, extended and COPY paths
