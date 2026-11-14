# Ryazan — refinement ideas

**Today:** TLS clients and servers on USSR socket handles through the
OS's TLS stack: Schannel on Windows, OpenSSL elsewhere, BearSSL in
Moscow. It supports TLS 1.2/1.3, checks the certificate chain and host
name, and takes `RYAZAN_CA_FILE` for private trust. Servers take
PKCS#12, or PEM with OpenSSL. It has a blocking HTTP/1.1 client
(`ryazan_request/get/post`) with status, body and header helpers. Text
ends at the first NUL; byte variants exist for Dubna.

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
- Certificate reload without a server restart
- ACME (Let's Encrypt) client to get and renew certificates for
  Novgorod
- A self-signed certificate generator for development
  (`ryazan_selfsign`)
- DTLS for UDP (Sochi over the internet)

## HTTP client
- Binary-safe bodies (byte vectors) in `ryazan_request`, not only text
  up to NUL
- Non-blocking/async `ryazan_request` that yields to the scheduler
- Keep-alive connection reuse and a small connection pool
- Redirect following with a limit, and same-origin rules for headers
- Timeouts: connect, read and total
- Proxy support (`HTTPS_PROXY`, CONNECT)
- gzip/deflate response decoding (Omsk has inflate)
- Chunked request bodies and streaming downloads to a file
- Multipart/form-data and URL-encoded form builders
- Cookie jar (optional)
- JSON helpers for AI provider calls
- HTTP/2 client once ALPN exists
- Retry with backoff on 429/503
- Parsing of the response into a struct (status, headers vector, body)

## Other protocols
- WebSocket client and server (`wss://`), for live Novgorod threads
- SMTP with STARTTLS for notifications
- An FTPS option, so Ural's upload can stop using plain FTP

## Diagnostics
- A verbose mode printing the handshake steps and the chosen suite
- Clear errors for common failures: expired, wrong host, unknown CA,
  protocol mismatch
- A `ryazan_check(url)` tool that reports the chain, expiry and
  supported versions

## Tests
- Add the loopback test to CI on a fixed free port, or with port 0 and
  discovery
- badssl.com-style negative cases against a local test server with bad
  certificates
- Interop tests between backends (Schannel client to OpenSSL server)

## Server side
- A generic TLS server helper: accept loop, handshake timeout, a
  handler per connection as a scheduler task (today each server writes
  its own around `ryazan_accept`)
- STARTTLS-style upgrade of a plain socket in the middle of a session
  (PostgreSQL's SSLRequest is done by hand in Dubna)
- HTTP to HTTPS redirect listener in one call
- Handshake rate limiting and a cap on concurrent handshakes
- Session cache sizing and ticket key rotation
- Keys in the OS key store or a PKCS#11 token
- Encrypted Client Hello and post-quantum key exchange when the
  backends offer them

## Client side, further
- DNS: several addresses tried in turn (happy eyeballs), IPv6, and a
  DNS-over-HTTPS resolver
- HSTS memory and a preload list for the client
- Certificate Transparency checks (SCTs present)
- A download helper with progress, resume and a hash check (Ural could
  use it in place of libcurl)
- Upload from a file handle, streamed
- `ryazan_request` options as a struct: timeout, redirects, CA file,
  client certificate, per request, in place of environment variables
- Environment `RYAZAN_CA_FILE` complemented by a per-call trust anchor

## Cryptography helpers
- Expose the backend's primitives to USSR: AEAD seal/open, X25519,
  Ed25519 sign/verify, random bytes (Ural signatures and Sochi
  encryption need them)
- Certificate and key parsing: PEM/DER to fields, PKCS#12 make and read
- A BearSSL build for hosted platforms, for one small dependency-free
  binary
- A feature table per backend (`tls_features(v):`), so scripts can
  check before they rely on ALPN or mTLS
