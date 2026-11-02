/*
 * tls.c - TLS for Moscow: BearSSL (https://bearssl.org, built by
 * src/moscow/build.sh into the same /lib/tls.so) behind a few functions
 * USSR calls with c_call. BearSSL does the cryptography and the TLS state
 * machine only; the bytes travel through the kernel's TCP in USSR
 * (src/moscow/tls.su): records come out of the engine (tls_sendrec) to
 * go on the socket, and what the socket brings goes in (tls_recvrec),
 * while the program's data goes in and out on the other side
 * (tls_sendapp, tls_recvapp).
 *
 * Every function takes integers, addresses and byte buffers: what c_call
 * passes. Memory comes from the caller (c_alloc of tls_conn_size(),
 * tls_creds_size(), tls_anchors_size()): nothing here allocates.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "bearssl.h"

#define TLS_CHAIN_MAX 8
#define TLS_DATA 65536
#define TLS_ANCHORS_MAX 32

typedef struct
{
    br_x509_certificate chain[TLS_CHAIN_MAX];
    size_t chain_len;
    int key_type;                       /* BR_KEYTYPE_RSA, _EC, 0 none */
    br_skey_decoder_context key;        /* holds the key's numbers */
    unsigned char data[TLS_DATA];       /* the certificates' DER */
    size_t used;
} tls_creds;

typedef struct
{
    br_x509_trust_anchor anchors[TLS_ANCHORS_MAX];
    size_t count;
    unsigned char data[TLS_DATA];       /* names and public keys */
    size_t used;
} tls_anchors;

typedef struct
{
    int server;
    br_ssl_server_context sc;
    br_ssl_client_context cc;
    br_x509_minimal_context xc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];
} tls_conn;

size_t tls_conn_size(void) { return sizeof(tls_conn); }
size_t tls_creds_size(void) { return sizeof(tls_creds); }
size_t tls_anchors_size(void) { return sizeof(tls_anchors); }

static br_ssl_engine_context *engine(tls_conn *t)
{
    return t->server ? &t->sc.eng : &t->cc.eng;
}

/* ---- PEM: the objects of a text, each handed to a function ---- */

typedef struct
{
    unsigned char *buf;
    size_t len;
    size_t cap;
    int overflow;
} pem_sink;

static void pem_append(void *ctx, const void *data, size_t len)
{
    pem_sink *s = ctx;

    if (s->len + len > s->cap)
    {
        s->overflow = 1;
        return;
    }
    memcpy(s->buf + s->len, data, len);
    s->len += len;
}

static int name_is(const char *a, const char *b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return *a == *b;
}

typedef int (*pem_object_fn)(void *ctx, const char *name,
                             unsigned char *der, size_t len);

/* Decodes every object of pem into scratch (cap bytes) and gives it to
 * fn: the number of objects, or -1 when the text is not PEM. */
static int pem_each(const char *pem, size_t len, unsigned char *scratch,
                    size_t cap, pem_object_fn fn, void *ctx)
{
    br_pem_decoder_context pc;
    pem_sink sink;
    char name[128];
    int count = 0;
    int inside = 0;

    br_pem_decoder_init(&pc);
    while (len > 0)
    {
        size_t used = br_pem_decoder_push(&pc, pem, len);

        pem += used;
        len -= used;
        switch (br_pem_decoder_event(&pc))
        {
        case BR_PEM_BEGIN_OBJ:
        {
            const char *n = br_pem_decoder_name(&pc);
            size_t k = 0;

            while (n[k] && k + 1 < sizeof name)
            {
                name[k] = n[k];
                ++k;
            }
            /* a file with CR LF line ends leaves BearSSL 0.6 the dashes
             * (and the CR) after the name: "CERTIFICATE-----" */
            while (k > 0 && (name[k - 1] == '-' || name[k - 1] == '\r' ||
                             name[k - 1] == ' '))
                --k;
            name[k] = 0;
            sink.buf = scratch;
            sink.len = 0;
            sink.cap = cap;
            sink.overflow = 0;
            br_pem_decoder_setdest(&pc, pem_append, &sink);
            inside = 1;
            break;
        }
        case BR_PEM_END_OBJ:
            if (inside && !sink.overflow)
            {
                if (fn(ctx, name, sink.buf, sink.len) < 0)
                    return -1;
                ++count;
            }
            inside = 0;
            break;
        case BR_PEM_ERROR:
            return -1;
        default:
            break;
        }
    }
    return count;
}

/* ---- server credentials ---- */

static int creds_object(void *ctx, const char *name, unsigned char *der,
                        size_t len)
{
    tls_creds *c = ctx;

    if (name_is(name, "CERTIFICATE"))
    {
        if (c->chain_len >= TLS_CHAIN_MAX || c->used + len > TLS_DATA)
            return -1;
        memcpy(c->data + c->used, der, len);
        c->chain[c->chain_len].data = c->data + c->used;
        c->chain[c->chain_len].data_len = len;
        c->chain_len++;
        c->used += len;
        return 0;
    }
    if (name_is(name, "PRIVATE KEY") || name_is(name, "RSA PRIVATE KEY") ||
        name_is(name, "EC PRIVATE KEY"))
    {
        br_skey_decoder_init(&c->key);
        br_skey_decoder_push(&c->key, der, len);
        if (br_skey_decoder_last_error(&c->key) != 0)
            return -1;
        c->key_type = br_skey_decoder_key_type(&c->key);
        return 0;
    }
    return 0;
}

/* Takes the certificates and the private key of a PEM text (call it for
 * the chain's file and for the key's): 0, or -1. */
int tls_creds_load(tls_creds *c, const char *pem, size_t len,
                   unsigned char *scratch, size_t cap)
{
    return pem_each(pem, len, scratch, cap, creds_object, c) < 0 ? -1 : 0;
}

/* 1 when there is a certificate and a key */
int tls_creds_ready(tls_creds *c)
{
    return c->chain_len > 0 && c->key_type != 0;
}

/* ---- trust anchors, for clients ---- */

typedef struct
{
    unsigned char *buf;
    size_t len;
    size_t cap;
} dn_sink;

static void dn_append(void *ctx, const void *data, size_t len)
{
    dn_sink *s = ctx;

    if (s->len + len <= s->cap)
        memcpy(s->buf + s->len, data, len);
    s->len += len;
}

static int anchor_object(void *ctx, const char *name, unsigned char *der,
                         size_t len)
{
    tls_anchors *a = ctx;
    br_x509_decoder_context dc;
    br_x509_trust_anchor *ta;
    br_x509_pkey *pk;
    dn_sink dn;

    if (!name_is(name, "CERTIFICATE"))
        return 0;
    if (a->count >= TLS_ANCHORS_MAX)
        return -1;
    dn.buf = a->data + a->used;
    dn.len = 0;
    dn.cap = TLS_DATA - a->used;
    br_x509_decoder_init(&dc, dn_append, &dn);
    br_x509_decoder_push(&dc, der, len);
    pk = br_x509_decoder_get_pkey(&dc);
    if (pk == NULL || dn.len > dn.cap)
        return -1;
    ta = &a->anchors[a->count];
    ta->dn.data = dn.buf;
    ta->dn.len = dn.len;
    a->used += dn.len;
    ta->flags = br_x509_decoder_isCA(&dc) ? BR_X509_TA_CA : 0;
    ta->pkey.key_type = pk->key_type;
    if (pk->key_type == BR_KEYTYPE_RSA)
    {
        if (a->used + pk->key.rsa.nlen + pk->key.rsa.elen > TLS_DATA)
            return -1;
        ta->pkey.key.rsa.n = a->data + a->used;
        memcpy(ta->pkey.key.rsa.n, pk->key.rsa.n, pk->key.rsa.nlen);
        ta->pkey.key.rsa.nlen = pk->key.rsa.nlen;
        a->used += pk->key.rsa.nlen;
        ta->pkey.key.rsa.e = a->data + a->used;
        memcpy(ta->pkey.key.rsa.e, pk->key.rsa.e, pk->key.rsa.elen);
        ta->pkey.key.rsa.elen = pk->key.rsa.elen;
        a->used += pk->key.rsa.elen;
    }
    else if (pk->key_type == BR_KEYTYPE_EC)
    {
        if (a->used + pk->key.ec.qlen > TLS_DATA)
            return -1;
        ta->pkey.key.ec.curve = pk->key.ec.curve;
        ta->pkey.key.ec.q = a->data + a->used;
        memcpy(ta->pkey.key.ec.q, pk->key.ec.q, pk->key.ec.qlen);
        ta->pkey.key.ec.qlen = pk->key.ec.qlen;
        a->used += pk->key.ec.qlen;
    }
    else
        return -1;
    a->count++;
    return 0;
}

/* Takes the certificates of a PEM text as trust anchors: how many there
 * are now, or -1. */
int tls_anchors_load(tls_anchors *a, const char *pem, size_t len,
                     unsigned char *scratch, size_t cap)
{
    if (pem_each(pem, len, scratch, cap, anchor_object, a) < 0)
        return -1;
    return (int)a->count;
}

/* ---- sessions ---- */

/* A server session with the credentials, seeded with seed: 0, or the
 * engine's error. */
int tls_server_start(tls_conn *t, tls_creds *c, const unsigned char *seed,
                     size_t seed_len)
{
    t->server = 1;
    if (c->key_type == BR_KEYTYPE_RSA)
        br_ssl_server_init_full_rsa(&t->sc, c->chain, c->chain_len,
                                    br_skey_decoder_get_rsa(&c->key));
    else if (c->key_type == BR_KEYTYPE_EC)
        br_ssl_server_init_full_ec(&t->sc, c->chain, c->chain_len,
                                   BR_KEYTYPE_EC,
                                   br_skey_decoder_get_ec(&c->key));
    else
        return -1;
    br_ssl_engine_set_buffer(&t->sc.eng, t->iobuf, sizeof t->iobuf, 1);
    br_ssl_engine_inject_entropy(&t->sc.eng, seed, seed_len);
    if (!br_ssl_server_reset(&t->sc))
        return br_ssl_engine_last_error(&t->sc.eng);
    return 0;
}

/* A client session for host, its server checked against the anchors at
 * days (since 1 January of year 0) and seconds (of that day): 0, or the
 * engine's error. */
int tls_client_start(tls_conn *t, const char *host, tls_anchors *a,
                     const unsigned char *seed, size_t seed_len,
                     uint32_t days, uint32_t seconds)
{
    t->server = 0;
    br_ssl_client_init_full(&t->cc, &t->xc, a->anchors, a->count);
    br_x509_minimal_set_time(&t->xc, days, seconds);
    br_ssl_engine_set_buffer(&t->cc.eng, t->iobuf, sizeof t->iobuf, 1);
    br_ssl_engine_inject_entropy(&t->cc.eng, seed, seed_len);
    if (!br_ssl_client_reset(&t->cc, host, 0))
        return br_ssl_engine_last_error(&t->cc.eng);
    return 0;
}

/* BR_SSL_CLOSED 1, SENDREC 2, RECVREC 4, SENDAPP 8, RECVAPP 16 */
int tls_state(tls_conn *t) { return (int)br_ssl_engine_current_state(engine(t)); }

int tls_error(tls_conn *t) { return br_ssl_engine_last_error(engine(t)); }

/* records for the peer, into out: how many bytes */
size_t tls_sendrec(tls_conn *t, unsigned char *out, size_t max)
{
    br_ssl_engine_context *e = engine(t);
    size_t len;
    unsigned char *buf;

    if (!(br_ssl_engine_current_state(e) & BR_SSL_SENDREC))
        return 0;
    buf = br_ssl_engine_sendrec_buf(e, &len);
    if (len > max)
        len = max;
    memcpy(out, buf, len);
    br_ssl_engine_sendrec_ack(e, len);
    return len;
}

/* bytes from the peer, into the engine: how many it took */
size_t tls_recvrec(tls_conn *t, const unsigned char *in, size_t len)
{
    br_ssl_engine_context *e = engine(t);
    size_t room;
    unsigned char *buf;

    if (!(br_ssl_engine_current_state(e) & BR_SSL_RECVREC))
        return 0;
    buf = br_ssl_engine_recvrec_buf(e, &room);
    if (len > room)
        len = room;
    memcpy(buf, in, len);
    br_ssl_engine_recvrec_ack(e, len);
    return len;
}

/* the program's bytes, into the engine: how many it took */
size_t tls_sendapp(tls_conn *t, const unsigned char *in, size_t len)
{
    br_ssl_engine_context *e = engine(t);
    size_t room;
    unsigned char *buf;

    if (!(br_ssl_engine_current_state(e) & BR_SSL_SENDAPP))
        return 0;
    buf = br_ssl_engine_sendapp_buf(e, &room);
    if (len > room)
        len = room;
    memcpy(buf, in, len);
    br_ssl_engine_sendapp_ack(e, len);
    br_ssl_engine_flush(e, 0);
    return len;
}

/* decrypted bytes for the program, into out: how many */
size_t tls_recvapp(tls_conn *t, unsigned char *out, size_t max)
{
    br_ssl_engine_context *e = engine(t);
    size_t len;
    unsigned char *buf;

    if (!(br_ssl_engine_current_state(e) & BR_SSL_RECVAPP))
        return 0;
    buf = br_ssl_engine_recvapp_buf(e, &len);
    if (len > max)
        len = max;
    memcpy(out, buf, len);
    br_ssl_engine_recvapp_ack(e, len);
    return len;
}

void tls_flush(tls_conn *t) { br_ssl_engine_flush(engine(t), 0); }

void tls_close(tls_conn *t) { br_ssl_engine_close(engine(t)); }

/* the protocol version << 16 | the cipher suite of a finished handshake */
long tls_info(tls_conn *t)
{
    br_ssl_engine_context *e = engine(t);
    br_ssl_session_parameters p;

    br_ssl_engine_get_session_parameters(e, &p);
    return ((long)br_ssl_engine_get_version(e) << 16) | p.cipher_suite;
}
