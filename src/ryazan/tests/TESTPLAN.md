# Ryazan test plan

## Automated

`tests/ryazan_loopback.su` runs offline on 127.0.0.1:18993 with the test
certificate in `tests/ryazan/`. That certificate is for localhost, valid for
100 years, and for tests only: its private key is public. The test prints
PASS/FAIL lines and exits with the number of failures. CI runs it on Linux
(OpenSSL) and Windows (Schannel). It checks that:

- a client refuses the self-signed certificate when it is not trusted;
- with `RYAZAN_CA_FILE` pointing at it, a PKCS#12 server and a client
  complete the handshake, report a cipher, and echo a message both ways;
- with OpenSSL, the same works with a PEM certificate and key;
- plain text sent to a TLS server is refused at once instead of hanging.

Like the socket and ukraine tests it is left out of `tests/run_all.sh`,
because it needs a free port. So are two more, run the same way:

- `tests/ryazan_http.su` (port 18997): the script client against fake
  servers in async tasks: Content-Length, chunked and close-delimited
  bodies, redirects (302/303 after POST, `max_redirects` 0), retries
  after 503, the cookie jar, keep-alive reuse, chunked uploads, binary
  mode, downloads with a SHA-256 check, a 1 s timeout, the proxy absolute
  form, a refused connection, JSON, and HTTPS with HSTS and a per-call
  CA file.
- `tests/ryazan_services.su` (port 18999): SHA-1 vectors and the
  WebSocket accept key, ws:// and wss:// echo through `ryazan_serve`,
  a client dropped by the handshake timeout, SMTP with STARTTLS and AUTH
  PLAIN against a fake server, a refused recipient, no password in the
  clear, and the HTTP-to-HTTPS redirect listener.

`tests/rzh_unit.su` needs no network and runs in `tests/run_all.sh`
(also through russia.su): URL parsing and resolution, cookies (domain,
path, Secure, Max-Age), HSTS, multipart and form bodies, error kinds,
`tls_features`, WebSocket frames and SMTP message text.

## Manual

- `ussr src/ryazan/examples/hello.su`: a real site (system roots, SNI,
  chunked bodies, `tls_info`).
- `ussr tests/ukraine_https.su`, then
  `curl --cacert tests/ryazan/localhost.cert.pem https://localhost:18996/`
  should answer 200. Without `--cacert`, curl must refuse the certificate.
  Plain `curl http://localhost:18996/` must fail at once.
- Rejections: `tls_request(r): "GET" "https://expired.badssl.com/"` and
  `https://wrong.host.badssl.com/` return `null`, and `tls_error` names the
  reason (expired certificate, name mismatch).
- `prompt(_):` against a provider (key set), or against a local
  OpenAI-compatible server over `http://`.

## Regenerating the test certificate

    cd tests/ryazan
    openssl req -x509 -newkey rsa:2048 -nodes -keyout localhost.key.pem \
        -out localhost.cert.pem -days 36500 -subj "/CN=localhost" \
        -addext "subjectAltName=DNS:localhost,IP:127.0.0.1"
    openssl pkcs12 -export -in localhost.cert.pem -inkey localhost.key.pem \
        -out localhost.p12 -passout pass:ryazan-test -name localhost
