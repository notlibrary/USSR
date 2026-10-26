# Ryazan

Ryazan is USSR's secure socket library: TLS clients and servers on ordinary
USSR socket handles, and HTTPS requests in one call. ukraine.su serves HTTPS
through it, and `prompt(_):` reaches AI providers through it.

```USSR
$include "src/ryazan/ryazan.su"

ryazan_get(page): "https://example.com/"
print(_): page
```

## How it keeps a connection secret

TLS does two jobs, and ryazan uses both:

- **Authentication, with asymmetric cryptography.** During the handshake the
  server proves it owns its certificate's private key (an RSA or ECDSA
  signature), and both sides agree on fresh session keys by ECDHE key
  agreement. Ryazan's client checks the certificate chain against the
  system's trusted roots and the name in it against the host it dialled.
  A forged, expired or wrong-host certificate ends the connection.
- **Confidentiality, with symmetric cryptography.** Every record after the
  handshake is encrypted and authenticated with those session keys under an
  AEAD cipher: AES-GCM or ChaCha20-Poly1305. `tls_info(i): socket` names the
  suite, for example `TLS 1.3, TLS_AES_256_GCM_SHA384`.

Ryazan does no cryptography of its own. Hand-written cryptography is where
security bugs live, so it drives the operating system's TLS stack:

| Platform | Backend  | Build |
|----------|----------|-------|
| Windows  | Schannel | always (`Makefile.msc`, or `ussr-ubuntu.mk` under MinGW) |
| Linux, BSD, macOS | OpenSSL 1.1.1+ | when `pkg-config openssl` finds it (`libssl-dev`); `RYAZAN_REQUIRE_TLS=1` makes it mandatory; autotools: `--with-openssl` / `--without-openssl` |
| anything else | none | `tls_backend(b):` answers `"none"`, and the calls fail with a message |

## Trust

Clients trust the system's root certificates: the Windows certificate store
under Schannel, and OpenSSL's default paths elsewhere (`SSL_CERT_FILE` and
`SSL_CERT_DIR` move them). Set `RYAZAN_CA_FILE` to a PEM file to trust only
the certificates in it, for example a private CA or a self-signed test
certificate. `setenv` changes it for the next connection.

## Serving

A server needs a certificate and its private key:

- **PKCS#12** (`.p12` or `.pfx`) with its password, on both backends. Convert
  PEM files with
  `openssl pkcs12 -export -in cert.pem -inkey key.pem -out server.p12`.
- **PEM** (OpenSSL only): the certificate chain file, and the key file as the
  secret (`""` when the key is in the same file).

Credentials are loaded once per file and cached. Under Schannel the key is
placed in the user's key store while the server runs and removed at exit.
Schannel serves only from a stored key, not from one held in memory.

```USSR
$include "src/ukraine/ukraine.su"
uk_route(result): method target raw_request [
    uk_response(result): "200 OK" "text/plain" "hello over TLS\n"
]
init(result): arg_cnt arg_vec [
    uk_server_tls(result): "0.0.0.0" 8443 "server.p12" "password"
]
```

## Files

- `ryazan.su`: the script library (see `docs/API.md`)
- `../ryazan.h`, `../ryazan.c`: sessions, buffering and the HTTP/1.1 client
- `../ryazan_schannel.c`, `../ryazan_openssl.c`: the two backends
- `examples/hello.su`: fetches a page and shows the negotiated cipher
- `tests/TESTPLAN.md`: what is tested and how; the tests themselves are
  `tests/ryazan_loopback.su` and `tests/ukraine_https.su`

## Limits

- IPv4 only, like the rest of the USSR socket API. Host names are resolved
  through the system resolver.
- USSR strings end at a zero byte, so binary bodies are cut at their first
  NUL byte. Text, JSON and HTML are fine.
- `ryazan_request`, `ryazan_get` and `ryazan_post` block the interpreter
  until they finish, up to 60 seconds by default. Use `ryazan_connect`,
  `ryazan_write` and `ryazan_read` in `async` tasks to overlap requests.
- No redirects, no HTTP keep-alive, no client certificates, no revocation
  checks (OCSP/CRL).
