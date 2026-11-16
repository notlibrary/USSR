# Ryazan — refinement ideas

**Today:** TLS clients and servers on USSR socket handles through the
OS's TLS stack: Schannel on Windows, OpenSSL elsewhere, BearSSL in
Moscow. It supports TLS 1.2/1.3, checks the certificate chain and host
name, and takes `RYAZAN_CA_FILE` for private trust. Servers take
PKCS#12, or PEM with OpenSSL. It has a blocking HTTP/1.1 client
(`ryazan_request/get/post`) with status, body and header helpers. Text
ends at the first NUL; byte variants exist for Dubna.

**Refine pass 1 (2026-11-16):** `[x]` done, `[~]` partly done; see
CHANGELOG.md 0.2.0. New script layers: `http.su` (client), `ws.su`,
`smtp.su` and `server.su`; the native TLS items (ALPN, mTLS, SNI on the
server, resumption, pinning) are still open.

## TLS
- ALPN negotiation (`h2`, `http/1.1`), as a client and as a server
- SNI on the server: several certificates chosen by host name
- Client certificates (mutual TLS) on both sides, with the verified peer
  identity exposed
- Session resumption (tickets, PSK) to speed reconnects
- Certificate pinning (SPKI hash) as an option
- OCSP stapling on the server and revocation checks on the client
- Minimum version and cipher-suite settings
- `tls_peer_cert(v): socket`: subject, issuer, validity, SANs and
  fingerprint
- [x] Certificate reload without a server restart
- ACME (Let's Encrypt) client to get and renew certificates for
  Novgorod
- A self-signed certificate generator for development
  (`ryazan_selfsign`)
- DTLS for UDP (Sochi over the internet)

## HTTP client
- [~] Binary-safe bodies (byte vectors) in `ryazan_request`, not only text
  up to NUL — partly: ryazan_fetch with opts binary returns bytes for Content-Length and close-delimited bodies (not chunked ones); ryazan_request itself is unchanged
- [x] Non-blocking/async `ryazan_request` that yields to the scheduler
- [x] Keep-alive connection reuse and a small connection pool
- [x] Redirect following with a limit, and same-origin rules for headers
- [x] Timeouts: connect, read and total
- [x] Proxy support (`HTTPS_PROXY`, CONNECT)
- gzip/deflate response decoding (Omsk has inflate)
- [~] Chunked request bodies and streaming downloads to a file — partly: chunked request bodies (vectors, body_file); downloads are binary-safe but collected before writing
- [x] Multipart/form-data and URL-encoded form builders
- [x] Cookie jar (optional)
- [x] JSON helpers for AI provider calls
- HTTP/2 client once ALPN exists
- [x] Retry with backoff on 429/503
- [x] Parsing of the response into a struct (status, headers vector, body)

## Other protocols
- [x] WebSocket client and server (`wss://`), for live Novgorod threads
- [x] SMTP with STARTTLS for notifications
- An FTPS option, so Ural's upload can stop using plain FTP

## Diagnostics
- [x] A verbose mode printing the handshake steps and the chosen suite
- [x] Clear errors for common failures: expired, wrong host, unknown CA,
  protocol mismatch
- [~] A `ryazan_check(url)` tool that reports the chain, expiry and
  supported versions — partly: reports backend, protocol, suite, status, HSTS and server; no chain or expiry (the backends do not expose them yet)

## Tests
- Add the loopback test to CI on a fixed free port, or with port 0 and
  discovery
- badssl.com-style negative cases against a local test server with bad
  certificates
- Interop tests between backends (Schannel client to OpenSSL server)

## Server side
- [x] A generic TLS server helper: accept loop, handshake timeout, a
  handler per connection as a scheduler task (today each server writes
  its own around `ryazan_accept`)
- [x] STARTTLS-style upgrade of a plain socket in the middle of a session
  (PostgreSQL's SSLRequest is done by hand in Dubna)
- [x] HTTP to HTTPS redirect listener in one call
- [~] Handshake rate limiting and a cap on concurrent handshakes — partly: max_concurrent cap and handshake_timeout in ryazan_serve; no rate limiting
- Session cache sizing and ticket key rotation
- Keys in the OS key store or a PKCS#11 token
- Encrypted Client Hello and post-quantum key exchange when the
  backends offer them

## Client side, further
- DNS: several addresses tried in turn (happy eyeballs), IPv6, and a
  DNS-over-HTTPS resolver
- [~] HSTS memory and a preload list for the client — partly: HSTS memory per process; no preload list
- Certificate Transparency checks (SCTs present)
- [~] A download helper with progress, resume and a hash check (Ural could
  use it in place of libcurl) — partly: ryazan_download checks a SHA-256 and prints the size; no resume
- [x] Upload from a file handle, streamed
- [x] `ryazan_request` options as a struct: timeout, redirects, CA file,
  client certificate, per request, in place of environment variables
- [x] Environment `RYAZAN_CA_FILE` complemented by a per-call trust anchor

## Cryptography helpers
- Expose the backend's primitives to USSR: AEAD seal/open, X25519,
  Ed25519 sign/verify, random bytes (Ural signatures and Sochi
  encryption need them)
- Certificate and key parsing: PEM/DER to fields, PKCS#12 make and read
- A BearSSL build for hosted platforms, for one small dependency-free
  binary
- [x] A feature table per backend (`tls_features(v):`), so scripts can
  check before they rely on ALPN or mTLS
