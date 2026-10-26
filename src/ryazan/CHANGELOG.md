# Ryazan changelog

## 0.1.0

- TLS clients and servers on USSR socket handles: Schannel on Windows,
  OpenSSL elsewhere. Certificate chain and host name checking, with
  `RYAZAN_CA_FILE` for private trust anchors. TLS 1.2 and 1.3.
- Builtins `socket_connect` (with DNS names) and `tls_*`, including the
  blocking HTTP/1.1 client `tls_request` for `http://` and `https://`.
- `ryazan.su` script library: connect/accept/read/write/close helpers, plus
  `ryazan_request`, `ryazan_get` and `ryazan_post` with response helpers.
- ukraine.su serves HTTPS through `uk_server_tls`; `prompt(_):` talks to AI
  providers through ryazan instead of the curl program.
