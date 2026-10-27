# Dubna test plan

Automated, no server needed:

- `ussr tests/dubna_test.su` (also through russia.su in `tests/run_all.sh`):
  the hash builtins against published vectors (SHA-256 one and two blocks,
  MD5, HMAC-SHA-256, PBKDF2 from RFC 7914/6070-style vectors including a
  40-byte key), `bytes_text` ranges and zero bytes; int16/int32 encoding
  both ways, base64 with padding and refusals; the exact bytes of the
  startup message, SSLRequest, a simple query and a Parse/Bind/Describe/
  Execute/Sync batch with a NULL parameter; the md5 answer; SCRAM-SHA-256
  against RFC 7677's example (client proof and server signature) and
  refusing a foreign nonce or a bad salt; nonces; ErrorResponse text and
  SQLSTATE; RowDescription and DataRow parsing, values by name and index,
  NULLs, counts from tags; parameter conversion; conninfo key=value with
  quoting and URLs with percent-escapes.

Against a server (not in `run_all.sh`; set `DUBNA_TEST_URL`):

- `ussr tests/dubna_live.su`: connect, simple and parameterised queries,
  UTF-8, NULL, a parameter full of SQL staying data, 500-row insert and a
  400-row select, an error with its SQLSTATE, rollback, the connection
  still usable after errors.
- `ussr tests/novgorod_db_live.su`: Novgorod with `--db` posts a thread and
  a reply, reloads them from the table, renders them, and prunes rows.

Done for 0.1.0 on a scratch PostgreSQL 18.4 cluster on Windows (Schannel):

- `dubna_live.su` passes as a SCRAM user over plain TCP, as an
  SSL-only SCRAM user through a `postgres://` URL with
  `sslmode=require` (test certificate from `tests/ryazan`, trusted with
  `RYAZAN_CA_FILE`), and as an md5 user (`prefer` picked TLS).
- Trust login works; a wrong password and a `pg_hba.conf` rejection come
  back as `FATAL 28P01` and `FATAL 28000`.
- `sslmode=prefer` with an untrusted certificate falls back to plain TCP;
  `require` refuses it with Schannel's reason.
- A 20 000-row result reads in about a second.
- `novgorod_db_live.su` passes; `novgorod.su --db` over HTTPS took a post
  through to the table and showed it. A role without CREATE on `public`
  (PostgreSQL 15+) gets a clear "permission denied for schema public" and
  Novgorod exits.
