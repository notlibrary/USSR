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
