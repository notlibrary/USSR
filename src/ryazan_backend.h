#ifndef USSR_RYAZAN_BACKEND_H
#define USSR_RYAZAN_BACKEND_H

/*
 * Between ryazan.c (sessions, buffering, socket I/O) and one TLS backend
 * (ryazan_schannel.c or ryazan_openssl.c). A backend never touches the
 * socket: it reads ciphertext from conn->in, appends ciphertext to
 * conn->out with rz_queue and plaintext to conn->plain with rz_plain_add.
 */

#include <stddef.h>

#if defined(USSR_TLS_OPENSSL)
#define RYAZAN_BACKEND_OPENSSL 1
#elif defined(_WIN32) && !defined(__wasi__) && !defined(USSR_TLS_NONE)
#define RYAZAN_BACKEND_SCHANNEL 1
#else
#define RYAZAN_BACKEND_NONE 1
#endif

typedef struct
{
    unsigned char *data;
    size_t length;
    size_t capacity;
} rz_buf_t;

typedef struct
{
    int socket;
    int server;
    int ready;          /* handshake finished                */
    int closed;         /* close_notify or EOF seen          */
    int hello_seen;     /* server: the first record was TLS  */
    rz_buf_t in;        /* ciphertext received, unprocessed  */
    rz_buf_t out;       /* ciphertext waiting to be sent     */
    size_t out_sent;    /* bytes of out already on the wire  */
    rz_buf_t plain;     /* decrypted, not yet read           */
    size_t plain_read;  /* bytes of plain already returned   */
    char host[256];
    void *backend;
} rz_conn_t;

/* backend step results */
enum
{
    RZ_OK = 0,          /* step finished (handshake done / records processed) */
    RZ_NEED_MORE = 1,   /* needs more ciphertext in conn->in                  */
    RZ_CLOSED = 2,      /* peer sent close_notify                             */
    RZ_FAIL = -1
};

/* helpers from ryazan.c */
int rz_append(rz_buf_t *buffer, const void *data, size_t length);
int rz_queue(rz_conn_t *conn, const void *data, size_t length);
int rz_plain_add(rz_conn_t *conn, const void *data, size_t length);
void rz_consume(rz_buf_t *buffer, size_t length);
void rz_fail(const char *format, ...);
int rz_read_file(const char *path, rz_buf_t *contents);
/* RYAZAN_CA_FILE as the script sees it (setenv included), malloc'd, or
 * NULL when unset or empty */
char *rz_ca_file(void);

/* the backend */
const char *rz_be_name(void);
int rz_be_client(rz_conn_t *conn);
int rz_be_server(rz_conn_t *conn, const char *credential, const char *secret);
int rz_be_handshake(rz_conn_t *conn);   /* RZ_OK, RZ_NEED_MORE or RZ_FAIL */
int rz_be_decrypt(rz_conn_t *conn);     /* RZ_OK, RZ_NEED_MORE, RZ_CLOSED or RZ_FAIL */
int rz_be_encrypt(rz_conn_t *conn, const void *data, size_t length); /* RZ_OK or RZ_FAIL */
void rz_be_shutdown(rz_conn_t *conn);   /* queues close_notify, best effort */
/* "TLS 1.3, TLS_AES_256_GCM_SHA384" for a finished handshake */
void rz_be_info(rz_conn_t *conn, char *text, size_t size);
void rz_be_free(rz_conn_t *conn);
void rz_be_cleanup(void);

#endif /* USSR_RYAZAN_BACKEND_H */
