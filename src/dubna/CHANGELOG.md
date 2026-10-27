# Changelog

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
