/*
 * ryazan_openssl.c - the OpenSSL TLS backend for ryazan (POSIX builds
 * define USSR_TLS_OPENSSL when libssl is found). TLS 1.2 and 1.3;
 * certificates are checked against the default verify paths
 * (SSL_CERT_FILE / SSL_CERT_DIR work), or against RYAZAN_CA_FILE.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L    /* strdup */
#endif

#include "ryazan_backend.h"

#ifdef RYAZAN_BACKEND_OPENSSL

#include <openssl/err.h>
#include <openssl/pkcs12.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    SSL *ssl;
    BIO *in;    /* ciphertext from the peer, written by us   */
    BIO *out;   /* ciphertext to the peer, read by us        */
} os_conn_t;

static SSL_CTX *client_context;
static char *client_ca_file;   /* RYAZAN_CA_FILE client_context was made for */

typedef struct os_server_cred
{
    char *credential;
    char *secret;
    SSL_CTX *context;
    struct os_server_cred *next;
} os_server_cred_t;

static os_server_cred_t *server_credentials;

const char *rz_be_name(void) { return "openssl"; }

static void os_fail(const char *what)
{
    unsigned long code = ERR_get_error();
    char reason[160];

    if (code != 0)
    {
        ERR_error_string_n(code, reason, sizeof(reason));
        rz_fail("%s: %s", what, reason);
    }
    else
        rz_fail("%s", what);
    ERR_clear_error();
}

static SSL_CTX *os_new_context(int server)
{
    SSL_CTX *context = SSL_CTX_new(server ? TLS_server_method() : TLS_client_method());

    if (context == NULL)
    {
        os_fail("cannot create a TLS context");
        return NULL;
    }
    SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION);
    SSL_CTX_set_mode(context, SSL_MODE_ENABLE_PARTIAL_WRITE |
                              SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    return context;
}

static int os_client_context(void)
{
    char *ca_file = rz_ca_file();

    if (client_context != NULL)
    {
        /* still made for the same trust anchors? */
        if ((ca_file == NULL && client_ca_file == NULL) ||
            (ca_file != NULL && client_ca_file != NULL && strcmp(ca_file, client_ca_file) == 0))
        {
            free(ca_file);
            return 0;
        }
        SSL_CTX_free(client_context);   /* live sessions keep their reference */
        client_context = NULL;
        free(client_ca_file);
        client_ca_file = NULL;
    }
    client_context = os_new_context(0);
    if (client_context == NULL)
    {
        free(ca_file);
        return -1;
    }
    SSL_CTX_set_verify(client_context, SSL_VERIFY_PEER, NULL);
    if (ca_file != NULL
            ? SSL_CTX_load_verify_locations(client_context, ca_file, NULL) != 1
            : SSL_CTX_set_default_verify_paths(client_context) != 1)
    {
        os_fail(ca_file != NULL ? "cannot load RYAZAN_CA_FILE"
                                : "cannot load the system CA certificates");
        SSL_CTX_free(client_context);
        client_context = NULL;
        free(ca_file);
        return -1;
    }
    client_ca_file = ca_file;
    return 0;
}

static int os_attach(rz_conn_t *conn, SSL_CTX *context)
{
    os_conn_t *os = calloc(1, sizeof(*os));

    if (os == NULL)
    {
        rz_fail("out of memory");
        return RZ_FAIL;
    }
    conn->backend = os;
    os->ssl = SSL_new(context);
    os->in = BIO_new(BIO_s_mem());
    os->out = BIO_new(BIO_s_mem());
    if (os->ssl == NULL || os->in == NULL || os->out == NULL)
    {
        BIO_free(os->in);
        BIO_free(os->out);
        os->in = os->out = NULL;
        os_fail("cannot create a TLS session");
        return RZ_FAIL;
    }
    BIO_set_mem_eof_return(os->in, -1);
    SSL_set_bio(os->ssl, os->in, os->out);  /* the SSL owns both now */
    return RZ_OK;
}

int rz_be_client(rz_conn_t *conn)
{
    os_conn_t *os;

    if (os_client_context() != 0 || os_attach(conn, client_context) != RZ_OK)
        return RZ_FAIL;
    os = (os_conn_t *)conn->backend;
    SSL_set_connect_state(os->ssl);
    if (SSL_set_tlsext_host_name(os->ssl, conn->host) != 1 ||
        SSL_set1_host(os->ssl, conn->host) != 1)
    {
        os_fail("bad host name");
        return RZ_FAIL;
    }
    return RZ_OK;
}

static int os_ends_with(const char *text, const char *suffix)
{
    size_t length = strlen(text);
    size_t suffix_length = strlen(suffix);
    size_t i;

    if (length < suffix_length)
        return 0;
    for (i = 0; i < suffix_length; ++i)
        if ((text[length - suffix_length + i] | 0x20) != (suffix[i] | 0x20))
            return 0;
    return 1;
}

static int os_load_pkcs12(SSL_CTX *context, const char *path, const char *password)
{
    FILE *file = fopen(path, "rb");
    PKCS12 *pkcs12;
    EVP_PKEY *key = NULL;
    X509 *certificate = NULL;
    STACK_OF(X509) *chain = NULL;
    int ok = 0;

    if (file == NULL)
    {
        rz_fail("cannot open %.200s", path);
        return -1;
    }
    pkcs12 = d2i_PKCS12_fp(file, NULL);
    fclose(file);
    if (pkcs12 == NULL || PKCS12_parse(pkcs12, password, &key, &certificate, &chain) != 1)
    {
        os_fail("cannot read the PKCS#12 file (wrong password?)");
        PKCS12_free(pkcs12);
        return -1;
    }
    PKCS12_free(pkcs12);
    if (certificate != NULL && key != NULL &&
        SSL_CTX_use_certificate(context, certificate) == 1 &&
        SSL_CTX_use_PrivateKey(context, key) == 1)
    {
        int i;

        ok = 1;
        for (i = 0; chain != NULL && i < sk_X509_num(chain); ++i)
        {
            X509 *ca = sk_X509_value(chain, i);

            if (SSL_CTX_add_extra_chain_cert(context, ca) != 1)
                ok = 0;
            else
                sk_X509_set(chain, i, NULL);   /* the context owns it */
        }
    }
    if (!ok)
        os_fail("cannot use the PKCS#12 certificate");
    X509_free(certificate);
    EVP_PKEY_free(key);
    sk_X509_pop_free(chain, X509_free);
    return ok ? 0 : -1;
}

static SSL_CTX *os_server_context(const char *credential, const char *secret)
{
    os_server_cred_t *entry;
    SSL_CTX *context;
    int loaded;

    for (entry = server_credentials; entry != NULL; entry = entry->next)
        if (strcmp(entry->credential, credential) == 0 && strcmp(entry->secret, secret) == 0)
            return entry->context;

    context = os_new_context(1);
    if (context == NULL)
        return NULL;
    if (os_ends_with(credential, ".p12") || os_ends_with(credential, ".pfx"))
        loaded = os_load_pkcs12(context, credential, secret);
    else
    {
        const char *key = secret[0] != '\0' ? secret : credential;

        loaded = SSL_CTX_use_certificate_chain_file(context, credential) == 1 &&
                         SSL_CTX_use_PrivateKey_file(context, key, SSL_FILETYPE_PEM) == 1
                     ? 0 : -1;
        if (loaded != 0)
            os_fail("cannot load the PEM certificate or key");
    }
    if (loaded == 0 && SSL_CTX_check_private_key(context) != 1)
    {
        os_fail("the private key does not match the certificate");
        loaded = -1;
    }
    if (loaded != 0)
    {
        SSL_CTX_free(context);
        return NULL;
    }
    entry = calloc(1, sizeof(*entry));
    if (entry == NULL)
    {
        SSL_CTX_free(context);
        rz_fail("out of memory");
        return NULL;
    }
    entry->credential = strdup(credential);
    entry->secret = strdup(secret);
    entry->context = context;
    entry->next = server_credentials;
    server_credentials = entry;
    return context;
}

int rz_be_server(rz_conn_t *conn, const char *credential, const char *secret)
{
    SSL_CTX *context = os_server_context(credential, secret);

    if (context == NULL || os_attach(conn, context) != RZ_OK)
        return RZ_FAIL;
    SSL_set_accept_state(((os_conn_t *)conn->backend)->ssl);
    return RZ_OK;
}

/* moves received ciphertext into OpenSSL */
static int os_feed(rz_conn_t *conn, os_conn_t *os)
{
    if (conn->in.length > 0)
    {
        int written = BIO_write(os->in, conn->in.data, (int)conn->in.length);

        if (written <= 0)
        {
            rz_fail("out of memory");
            return -1;
        }
        rz_consume(&conn->in, (size_t)written);
    }
    return 0;
}

/* moves OpenSSL's ciphertext into the send queue */
static int os_drain(rz_conn_t *conn, os_conn_t *os)
{
    char chunk[16384];
    int got;

    while ((got = BIO_read(os->out, chunk, sizeof(chunk))) > 0)
        if (rz_queue(conn, chunk, (size_t)got) != 0)
            return -1;
    return 0;
}

int rz_be_handshake(rz_conn_t *conn)
{
    os_conn_t *os = (os_conn_t *)conn->backend;
    int result;
    int error;

    if (os_feed(conn, os) != 0)
        return RZ_FAIL;
    result = SSL_do_handshake(os->ssl);
    if (os_drain(conn, os) != 0)
        return RZ_FAIL;
    if (result == 1)
        return RZ_OK;
    error = SSL_get_error(os->ssl, result);
    if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
        return RZ_NEED_MORE;
    if (SSL_get_verify_result(os->ssl) != X509_V_OK)
        rz_fail("certificate of %s rejected: %s", conn->host,
                X509_verify_cert_error_string(SSL_get_verify_result(os->ssl)));
    else
        os_fail("TLS handshake failed");
    ERR_clear_error();
    return RZ_FAIL;
}

int rz_be_decrypt(rz_conn_t *conn)
{
    os_conn_t *os = (os_conn_t *)conn->backend;
    char chunk[16384];

    if (os_feed(conn, os) != 0)
        return RZ_FAIL;
    for (;;)
    {
        int got = SSL_read(os->ssl, chunk, sizeof(chunk));
        int error;

        if (got > 0)
        {
            if (rz_plain_add(conn, chunk, (size_t)got) != 0)
                return RZ_FAIL;
            continue;
        }
        if (os_drain(conn, os) != 0)
            return RZ_FAIL;
        error = SSL_get_error(os->ssl, got);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE)
            return RZ_NEED_MORE;
        if (error == SSL_ERROR_ZERO_RETURN)
            return RZ_CLOSED;
        os_fail("TLS decrypt failed");
        return RZ_FAIL;
    }
}

int rz_be_encrypt(rz_conn_t *conn, const void *data, size_t length)
{
    os_conn_t *os = (os_conn_t *)conn->backend;
    size_t offset = 0;

    while (offset < length)
    {
        size_t piece = length - offset;
        int written;

        if (piece > 16384)
            piece = 16384;
        written = SSL_write(os->ssl, (const char *)data + offset, (int)piece);
        if (written <= 0)
        {
            os_fail("TLS encrypt failed");
            return RZ_FAIL;
        }
        offset += (size_t)written;
        if (os_drain(conn, os) != 0)
            return RZ_FAIL;
    }
    return RZ_OK;
}

void rz_be_shutdown(rz_conn_t *conn)
{
    os_conn_t *os = (os_conn_t *)conn->backend;

    if (os == NULL || os->ssl == NULL)
        return;
    (void)SSL_shutdown(os->ssl);
    (void)os_drain(conn, os);
    ERR_clear_error();
}

void rz_be_info(rz_conn_t *conn, char *text, size_t size)
{
    os_conn_t *os = (os_conn_t *)conn->backend;
    const char *version;
    const char *suite;

    text[0] = '\0';
    if (os == NULL || os->ssl == NULL)
        return;
    version = SSL_get_version(os->ssl);
    suite = SSL_get_cipher_name(os->ssl);
    /* OpenSSL says "TLSv1.3"; match Schannel's "TLS 1.3" */
    snprintf(text, size, "TLS %s, %s",
             strncmp(version, "TLSv", 4) == 0 ? version + 4 : version,
             suite != NULL ? suite : "");
}

void rz_be_free(rz_conn_t *conn)
{
    os_conn_t *os = (os_conn_t *)conn->backend;

    if (os == NULL)
        return;
    if (os->ssl != NULL)
        SSL_free(os->ssl);  /* frees both BIOs */
    free(os);
    conn->backend = NULL;
}

void rz_be_cleanup(void)
{
    while (server_credentials != NULL)
    {
        os_server_cred_t *next = server_credentials->next;

        SSL_CTX_free(server_credentials->context);
        free(server_credentials->credential);
        if (server_credentials->secret != NULL)
            memset(server_credentials->secret, 0, strlen(server_credentials->secret));
        free(server_credentials->secret);
        free(server_credentials);
        server_credentials = next;
    }
    if (client_context != NULL)
    {
        SSL_CTX_free(client_context);
        client_context = NULL;
    }
    free(client_ca_file);
    client_ca_file = NULL;
}

#else

/* ISO C forbids an empty translation unit */
typedef int ryazan_openssl_unused;

#endif /* RYAZAN_BACKEND_OPENSSL */
