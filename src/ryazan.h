#ifndef USSR_RYAZAN_H
#define USSR_RYAZAN_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ryazan: TLS over USSR socket handles.
 *
 * The handshake authenticates the peer with its certificate (asymmetric
 * cryptography: RSA or ECDSA signatures, ECDHE key agreement) and agrees
 * on session keys; records then travel under a symmetric AEAD cipher
 * (AES-GCM or ChaCha20-Poly1305). Ryazan does no cryptography of its own:
 * it drives the operating system's TLS stack, Schannel on Windows and
 * OpenSSL elsewhere (build with USSR_TLS_OPENSSL). A build with neither
 * reports "none" and every call fails with a message.
 *
 * A TLS session is attached to an existing socket handle and is named by
 * that same handle, so wait_readable/wait_writable keep working on it.
 * Every call is nonblocking; the RYAZAN_WANT_* results say what to wait
 * for before calling again.
 *
 * Clients verify the server's certificate chain against the system's
 * trusted roots and its name against the host given to
 * ryazan_tls_client. RYAZAN_CA_FILE, when set to a PEM file, replaces the
 * system roots with the certificates in that file (a private CA or a
 * self-signed test certificate).
 */

enum
{
    RYAZAN_DONE = 1,        /* handshake finished / all output sent */
    RYAZAN_WANT_READ = 0,   /* wait until the socket is readable    */
    RYAZAN_WANT_WRITE = 2,  /* wait until the socket is writable    */
    RYAZAN_ERROR = -1
};

/* "schannel", "openssl" or "none". */
const char *ryazan_backend(void);

/* Starts a client session on a connected socket; host is the name the
 * server's certificate must carry (also sent as SNI). 0 or -1. */
int ryazan_tls_client(int socket, const char *host);

/* Starts a server session on an accepted socket. credential is either a
 * PKCS#12 file (.p12 or .pfx; secret is its password) or, with OpenSSL,
 * a PEM certificate chain (secret is the PEM private key file, or "" when
 * the key is in the same file). Credentials are loaded once and cached.
 * 0 or -1. */
int ryazan_tls_server(int socket, const char *credential, const char *secret);

/* Advances the handshake: RYAZAN_DONE, RYAZAN_WANT_READ,
 * RYAZAN_WANT_WRITE or RYAZAN_ERROR. */
int ryazan_tls_handshake(int socket);

/* Decrypted bytes: > 0 read, 0 the peer closed, RYAZAN_ERROR, or
 * -2 (RYAZAN_RECV_WANT_READ) when nothing has arrived yet. */
#define RYAZAN_RECV_WANT_READ (-2)
long ryazan_tls_recv(int socket, void *buffer, size_t capacity);

/* Encrypts and queues up to length bytes; returns how many were taken
 * (0 when earlier output is still waiting: call ryazan_tls_flush), or
 * RYAZAN_ERROR. */
long ryazan_tls_send(int socket, const void *buffer, size_t length);

/* Sends queued output: RYAZAN_DONE, RYAZAN_WANT_WRITE or RYAZAN_ERROR. */
int ryazan_tls_flush(int socket);

/* Decrypted bytes already buffered: read them before waiting on the
 * socket, which may never become readable for them. */
size_t ryazan_tls_pending(int socket);

/* Protocol and cipher suite of a finished handshake, for example
 * "TLS 1.3, TLS_AES_256_GCM_SHA384"; "" when there is none. */
const char *ryazan_tls_info(int socket);

/* 1 when socket has a TLS session. */
int ryazan_tls_active(int socket);

/* Sends close_notify (best effort) and drops the session; the socket
 * itself stays open. 0, or -1 when there was no session. */
int ryazan_tls_close(int socket);

/* The last error message, and a way to replace it. */
const char *ryazan_last_error(void);
void ryazan_set_error(const char *message);

/*
 * Blocking HTTP/1.1 client over ryazan (https://) or plain TCP (http://),
 * for C callers such as prompt(_):. headers holds extra request header
 * lines, each ending in "\r\n" (may be NULL); body may be NULL. Follows no
 * redirects. On success returns 0 and fills response; on failure returns
 * -1 with a message in error.
 */
typedef struct
{
    long status;        /* 200, 404, ...                          */
    char *head;         /* status line and headers, CRLF-separated */
    char *body;         /* decoded body (chunked removed), NUL-ended */
    size_t body_length;
} ryazan_response_t;

int ryazan_http_request(const char *method, const char *url,
                        const char *headers, const char *body,
                        size_t body_length, int timeout_seconds,
                        ryazan_response_t *response,
                        char *error, size_t error_size);
void ryazan_response_free(ryazan_response_t *response);

/* Releases cached credentials and every session (at shutdown). */
void ryazan_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* USSR_RYAZAN_H */
