/*
 * ryazan.c - TLS sessions on USSR socket handles, and a small blocking
 * HTTP/1.1 client on top of them. The cryptography is the backend's
 * (ryazan_schannel.c or ryazan_openssl.c); this file owns the session
 * table, the ciphertext/plaintext buffers and all socket I/O.
 */

#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L
#endif

#include "ryazan.h"
#include "ryazan_backend.h"
#include "socket.h"
#include "ussr_oop_builtins.h" /* ussr_env_get */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RZ_SLOTS 4096                       /* matches the socket tables */
#define RZ_READ_CHUNK 16384
#define RZ_MAX_RESPONSE (64u * 1024u * 1024u)

static rz_conn_t *sessions[RZ_SLOTS];
static char error_text[256];

/* ------------------------------------------------------------- */
/* helpers shared with the backends                                 */
/* ------------------------------------------------------------- */

void rz_fail(const char *format, ...)
{
    va_list args;

    va_start(args, format);
    vsnprintf(error_text, sizeof(error_text), format, args);
    va_end(args);
}

void ryazan_set_error(const char *message)
{
    rz_fail("%s", message);
}

const char *ryazan_last_error(void)
{
    return error_text[0] ? error_text : "no TLS error";
}

static int rz_reserve(rz_buf_t *buffer, size_t extra)
{
    size_t capacity;
    unsigned char *grown;

    if (buffer->length + extra <= buffer->capacity)
        return 0;
    capacity = buffer->capacity ? buffer->capacity : 4096;
    while (capacity < buffer->length + extra)
        capacity *= 2;
    grown = realloc(buffer->data, capacity + 1);
    if (grown == NULL)
    {
        rz_fail("out of memory");
        return -1;
    }
    buffer->data = grown;
    buffer->capacity = capacity;
    return 0;
}

int rz_append(rz_buf_t *buffer, const void *data, size_t length)
{
    if (length == 0)
        return 0;
    if (rz_reserve(buffer, length) != 0)
        return -1;
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    buffer->data[buffer->length] = '\0';
    return 0;
}

int rz_queue(rz_conn_t *conn, const void *data, size_t length)
{
    return rz_append(&conn->out, data, length);
}

int rz_plain_add(rz_conn_t *conn, const void *data, size_t length)
{
    return rz_append(&conn->plain, data, length);
}

void rz_consume(rz_buf_t *buffer, size_t length)
{
    if (length >= buffer->length)
    {
        buffer->length = 0;
        return;
    }
    memmove(buffer->data, buffer->data + length, buffer->length - length);
    buffer->length -= length;
}

int rz_read_file(const char *path, rz_buf_t *contents)
{
    FILE *file = fopen(path, "rb");
    unsigned char chunk[4096];
    size_t got;

    if (file == NULL)
    {
        rz_fail("cannot open %.200s", path);
        return -1;
    }
    while ((got = fread(chunk, 1, sizeof(chunk), file)) > 0)
    {
        if (rz_append(contents, chunk, got) != 0)
        {
            fclose(file);
            return -1;
        }
    }
    fclose(file);
    if (contents->data == NULL && rz_reserve(contents, 1) != 0)
        return -1;
    contents->data[contents->length] = '\0';
    return 0;
}

char *rz_ca_file(void)
{
    char *path = ussr_env_get("RYAZAN_CA_FILE");

    if (path != NULL && path[0] == '\0')
    {
        free(path);
        path = NULL;
    }
    return path;
}

static void rz_buf_free(rz_buf_t *buffer)
{
    if (buffer->data != NULL)
        memset(buffer->data, 0, buffer->capacity);
    free(buffer->data);
    buffer->data = NULL;
    buffer->length = buffer->capacity = 0;
}

/* ------------------------------------------------------------- */
/* sessions                                                         */
/* ------------------------------------------------------------- */

const char *ryazan_backend(void)
{
    return rz_be_name();
}

static rz_conn_t *rz_get(int socket)
{
    if (socket < 1 || socket > RZ_SLOTS || sessions[socket - 1] == NULL)
    {
        rz_fail("socket %d has no TLS session", socket);
        return NULL;
    }
    return sessions[socket - 1];
}

static void rz_free(rz_conn_t *conn)
{
    rz_be_free(conn);
    rz_buf_free(&conn->in);
    rz_buf_free(&conn->out);
    rz_buf_free(&conn->plain);
    free(conn);
}

static rz_conn_t *rz_new(int socket, int server, const char *host)
{
    rz_conn_t *conn;

    if (socket < 1 || socket > RZ_SLOTS)
    {
        rz_fail("invalid socket handle %d", socket);
        return NULL;
    }
    if (sessions[socket - 1] != NULL)
    {
        rz_fail("socket %d already has a TLS session", socket);
        return NULL;
    }
    conn = calloc(1, sizeof(*conn));
    if (conn == NULL)
    {
        rz_fail("out of memory");
        return NULL;
    }
    conn->socket = socket;
    conn->server = server;
    if (host != NULL)
        snprintf(conn->host, sizeof(conn->host), "%s", host);
    return conn;
}

int ryazan_tls_client(int socket, const char *host)
{
    rz_conn_t *conn;

    if (host == NULL || host[0] == '\0' || strlen(host) >= 255)
    {
        rz_fail("TLS client needs the server's host name");
        return -1;
    }
    conn = rz_new(socket, 0, host);
    if (conn == NULL)
        return -1;
    if (rz_be_client(conn) != RZ_OK)
    {
        rz_free(conn);
        return -1;
    }
    sessions[socket - 1] = conn;
    return 0;
}

int ryazan_tls_server(int socket, const char *credential, const char *secret)
{
    rz_conn_t *conn;

    if (credential == NULL || credential[0] == '\0')
    {
        rz_fail("TLS server needs a certificate file");
        return -1;
    }
    conn = rz_new(socket, 1, NULL);
    if (conn == NULL)
        return -1;
    if (rz_be_server(conn, credential, secret ? secret : "") != RZ_OK)
    {
        rz_free(conn);
        return -1;
    }
    sessions[socket - 1] = conn;
    return 0;
}

const char *ryazan_tls_info(int socket)
{
    static char text[160];

    text[0] = '\0';
    if (ryazan_tls_active(socket) && sessions[socket - 1]->ready)
        rz_be_info(sessions[socket - 1], text, sizeof(text));
    return text;
}

int ryazan_tls_active(int socket)
{
    return socket >= 1 && socket <= RZ_SLOTS && sessions[socket - 1] != NULL;
}

/* Sends queued ciphertext: RYAZAN_DONE, RYAZAN_WANT_WRITE or RYAZAN_ERROR. */
static int rz_flush(rz_conn_t *conn)
{
    while (conn->out_sent < conn->out.length)
    {
        long sent = ussr_socket_send(conn->socket,
                                     conn->out.data + conn->out_sent,
                                     conn->out.length - conn->out_sent);
        if (sent <= 0)
        {
            if (sent < 0 && ussr_socket_would_block())
                return RYAZAN_WANT_WRITE;
            rz_fail("TLS send failed: %s", ussr_socket_last_error());
            return RYAZAN_ERROR;
        }
        conn->out_sent += (size_t)sent;
    }
    conn->out.length = 0;
    conn->out_sent = 0;
    return RYAZAN_DONE;
}

/* Reads ciphertext: > 0 got bytes, 0 EOF, -2 would block, -1 error. */
static long rz_fill(rz_conn_t *conn)
{
    long got;

    if (rz_reserve(&conn->in, RZ_READ_CHUNK) != 0)
        return -1;
    got = ussr_socket_recv(conn->socket, conn->in.data + conn->in.length,
                           RZ_READ_CHUNK);
    if (got < 0)
    {
        if (ussr_socket_would_block())
            return -2;
        rz_fail("TLS receive failed: %s", ussr_socket_last_error());
        return -1;
    }
    conn->in.length += (size_t)got;
    return got;
}

int ryazan_tls_handshake(int socket)
{
    rz_conn_t *conn = rz_get(socket);

    if (conn == NULL)
        return RYAZAN_ERROR;
    for (;;)
    {
        int step;
        int flushed = rz_flush(conn);
        long got;

        if (flushed != RYAZAN_DONE)
            return flushed;
        if (conn->ready)
            return RYAZAN_DONE;
        step = rz_be_handshake(conn);
        if (step == RZ_FAIL)
        {
            (void)rz_flush(conn);   /* deliver the alert, if any */
            return RYAZAN_ERROR;
        }
        if (step == RZ_OK)
        {
            conn->ready = 1;
            continue;
        }
        flushed = rz_flush(conn);
        if (flushed != RYAZAN_DONE)
            return flushed;
        got = rz_fill(conn);
        if (got == -2)
            return RYAZAN_WANT_READ;
        if (got == 0)
        {
            rz_fail("the peer closed the connection during the TLS handshake");
            return RYAZAN_ERROR;
        }
        if (got < 0)
            return RYAZAN_ERROR;
        /* a ClientHello starts with a handshake record (0x16); anything
         * else, like plain HTTP on the HTTPS port, would otherwise wait
         * for a record that never comes */
        if (conn->server && !conn->hello_seen)
        {
            if (conn->in.data[0] != 0x16)
            {
                rz_fail("the client did not start a TLS handshake (plain HTTP?)");
                return RYAZAN_ERROR;
            }
            conn->hello_seen = 1;
        }
    }
}

static long rz_take_plain(rz_conn_t *conn, void *buffer, size_t capacity)
{
    size_t available = conn->plain.length - conn->plain_read;

    if (available > capacity)
        available = capacity;
    memcpy(buffer, conn->plain.data + conn->plain_read, available);
    conn->plain_read += available;
    if (conn->plain_read == conn->plain.length)
        conn->plain.length = conn->plain_read = 0;
    return (long)available;
}

long ryazan_tls_recv(int socket, void *buffer, size_t capacity)
{
    rz_conn_t *conn = rz_get(socket);

    if (conn == NULL || buffer == NULL || capacity == 0)
        return RYAZAN_ERROR;
    if (!conn->ready)
    {
        rz_fail("the TLS handshake has not finished");
        return RYAZAN_ERROR;
    }
    for (;;)
    {
        long got;

        if (conn->plain.length > conn->plain_read)
            return rz_take_plain(conn, buffer, capacity);
        if (conn->closed)
            return 0;
        if (conn->in.length > 0)
        {
            int step = rz_be_decrypt(conn);

            if (step == RZ_FAIL)
                return RYAZAN_ERROR;
            if (step == RZ_CLOSED)
                conn->closed = 1;
            /* post-handshake messages may have queued a reply */
            if (rz_flush(conn) == RYAZAN_ERROR)
                return RYAZAN_ERROR;
            if (conn->plain.length > conn->plain_read || conn->closed)
                continue;
        }
        got = rz_fill(conn);
        if (got == -2)
            return RYAZAN_RECV_WANT_READ;
        if (got < 0)
            return RYAZAN_ERROR;
        if (got == 0)
            conn->closed = 1;   /* EOF without close_notify */
    }
}

long ryazan_tls_send(int socket, const void *buffer, size_t length)
{
    rz_conn_t *conn = rz_get(socket);
    int flushed;

    if (conn == NULL)
        return RYAZAN_ERROR;
    if (!conn->ready)
    {
        rz_fail("the TLS handshake has not finished");
        return RYAZAN_ERROR;
    }
    flushed = rz_flush(conn);
    if (flushed == RYAZAN_ERROR)
        return RYAZAN_ERROR;
    if (flushed == RYAZAN_WANT_WRITE)
        return 0;
    if (length == 0)
        return 0;
    if (length > 1024 * 1024)
        length = 1024 * 1024;
    if (rz_be_encrypt(conn, buffer, length) != RZ_OK)
        return RYAZAN_ERROR;
    if (rz_flush(conn) == RYAZAN_ERROR)
        return RYAZAN_ERROR;
    return (long)length;
}

int ryazan_tls_flush(int socket)
{
    rz_conn_t *conn = rz_get(socket);

    if (conn == NULL)
        return RYAZAN_ERROR;
    return rz_flush(conn);
}

size_t ryazan_tls_pending(int socket)
{
    rz_conn_t *conn;

    if (!ryazan_tls_active(socket))
        return 0;
    conn = sessions[socket - 1];
    return conn->plain.length - conn->plain_read;
}

int ryazan_tls_close(int socket)
{
    rz_conn_t *conn;

    if (!ryazan_tls_active(socket))
        return -1;
    conn = sessions[socket - 1];
    if (conn->ready)
    {
        rz_be_shutdown(conn);
        (void)rz_flush(conn);
    }
    sessions[socket - 1] = NULL;
    rz_free(conn);
    return 0;
}

void ryazan_cleanup(void)
{
    int i;

    for (i = 0; i < RZ_SLOTS; ++i)
    {
        if (sessions[i] != NULL)
        {
            rz_free(sessions[i]);
            sessions[i] = NULL;
        }
    }
    rz_be_cleanup();
}

/* ------------------------------------------------------------- */
/* blocking HTTP/1.1 client                                         */
/* ------------------------------------------------------------- */

typedef struct
{
    int https;
    char host[256];
    unsigned short port;
    const char *target;     /* points into the URL; "" means "/" */
} rz_url_t;

static int rz_parse_url(const char *url, rz_url_t *out,
                        char *error, size_t error_size)
{
    const char *host;
    const char *end;
    size_t host_length;
    long port;

    memset(out, 0, sizeof(*out));
    if (strncmp(url, "https://", 8) == 0)
    {
        out->https = 1;
        out->port = 443;
        host = url + 8;
    }
    else if (strncmp(url, "http://", 7) == 0)
    {
        out->port = 80;
        host = url + 7;
    }
    else
    {
        snprintf(error, error_size, "only http:// and https:// URLs are supported: %.200s", url);
        return -1;
    }
    end = host + strcspn(host, ":/?#");
    host_length = (size_t)(end - host);
    if (host_length == 0 || host_length >= sizeof(out->host) ||
        memchr(host, '@', host_length) != NULL)
    {
        snprintf(error, error_size, "bad host in URL %.200s", url);
        return -1;
    }
    memcpy(out->host, host, host_length);
    out->host[host_length] = '\0';
    if (*end == ':')
    {
        char *digits_end;

        port = strtol(end + 1, &digits_end, 10);
        if (digits_end == end + 1 || port < 1 || port > 65535 ||
            (*digits_end != '\0' && *digits_end != '/' && *digits_end != '?'))
        {
            snprintf(error, error_size, "bad port in URL %.200s", url);
            return -1;
        }
        out->port = (unsigned short)port;
        end = digits_end;
    }
    out->target = end;
    return 0;
}

/* Waits for the socket to be ready; 0 ready, -1 timeout or error. */
static int rz_wait(int socket, int write, time_t deadline)
{
    ussr_socket_pollfd_t poll_fd;
    time_t now = time(NULL);
    int ready;

    if (now >= deadline)
        return -1;
    poll_fd.handle = socket;
    poll_fd.events = (short)(write ? USSR_SOCKET_WRITE : USSR_SOCKET_READ);
    poll_fd.revents = 0;
    ready = ussr_socket_poll(&poll_fd, 1, (int)(deadline - now) * 1000);
    return ready > 0 ? 0 : -1;
}

static int rz_has_header(const char *headers, const char *name)
{
    size_t length = strlen(name);
    const char *line = headers;

    while (line != NULL && *line != '\0')
    {
        size_t i;

        for (i = 0; i < length && line[i] != '\0' &&
                    tolower((unsigned char)line[i]) == name[i]; ++i)
            ;
        if (i == length && line[i] == ':')
            return 1;
        line = strstr(line, "\r\n");
        if (line != NULL)
            line += 2;
    }
    return 0;
}

/* Value of a response header (case-insensitive name), malloc'd, or NULL. */
static char *rz_header_value(const char *head, const char *name)
{
    size_t length = strlen(name);
    const char *line = strstr(head, "\r\n");

    while (line != NULL && line[2] != '\0')
    {
        size_t i;

        line += 2;
        for (i = 0; i < length &&
                    tolower((unsigned char)line[i]) == tolower((unsigned char)name[i]); ++i)
            ;
        if (i == length && line[i] == ':')
        {
            const char *value = line + i + 1;
            const char *stop = strstr(value, "\r\n");
            size_t value_length;
            char *copy;

            while (*value == ' ' || *value == '\t')
                ++value;
            value_length = stop ? (size_t)(stop - value) : strlen(value);
            while (value_length > 0 &&
                   (value[value_length - 1] == ' ' || value[value_length - 1] == '\t'))
                --value_length;
            copy = malloc(value_length + 1);
            if (copy != NULL)
            {
                memcpy(copy, value, value_length);
                copy[value_length] = '\0';
            }
            return copy;
        }
        line = strstr(line, "\r\n");
    }
    return NULL;
}

/* Decodes a complete chunked body in place; returns its length, or -1
 * when the body is malformed, or -2 when it is not complete yet. */
static long rz_dechunk(unsigned char *body, size_t length)
{
    size_t in = 0;
    size_t out = 0;

    for (;;)
    {
        size_t size = 0;
        int digits = 0;

        while (in < length && isxdigit(body[in]))
        {
            int c = tolower(body[in]);

            if (size > (RZ_MAX_RESPONSE >> 4))
                return -1;
            size = size * 16 + (size_t)(c <= '9' ? c - '0' : c - 'a' + 10);
            ++in;
            ++digits;
        }
        if (in >= length)
            return -2;
        if (digits == 0)
            return -1;
        while (in < length && body[in] != '\n')   /* chunk extensions */
            ++in;
        if (in >= length)
            return -2;
        ++in;
        if (size == 0)
            return (long)out;    /* trailers are ignored */
        if (length - in < size + 2)
            return -2;
        memmove(body + out, body + in, size);
        out += size;
        in += size;
        if (body[in] == '\r')
            ++in;
        if (in >= length || body[in] != '\n')
            return -1;
        ++in;
    }
}

/* 1 when the response in raw is complete, 0 when more is needed. */
static int rz_response_complete(const rz_buf_t *raw)
{
    const char *end;
    char *value;
    size_t head_length;
    int complete = 0;

    if (raw->data == NULL)
        return 0;
    end = strstr((const char *)raw->data, "\r\n\r\n");
    if (end == NULL)
        return 0;
    head_length = (size_t)(end - (const char *)raw->data) + 4;
    {
        char *head = malloc(head_length + 1);

        if (head == NULL)
            return 0;
        memcpy(head, raw->data, head_length);
        head[head_length] = '\0';
        value = rz_header_value(head, "content-length");
        if (value != NULL)
        {
            complete = raw->length - head_length >= (size_t)strtoul(value, NULL, 10);
            free(value);
        }
        else if ((value = rz_header_value(head, "transfer-encoding")) != NULL)
        {
            if (strstr(value, "chunked") != NULL)
            {
                size_t body_length = raw->length - head_length;
                unsigned char *copy = malloc(body_length + 1);

                if (copy != NULL)
                {
                    memcpy(copy, raw->data + head_length, body_length);
                    complete = rz_dechunk(copy, body_length) != -2;
                    free(copy);
                }
            }
            free(value);
        }
        else if (head_length > 12 &&
                 (strncmp(head + 9, "204", 3) == 0 || strncmp(head + 9, "304", 3) == 0))
            complete = 1;
        free(head);
    }
    return complete;
}

static int rz_send_all(int socket, int tls, const char *data, size_t length,
                       time_t deadline, char *error, size_t error_size)
{
    size_t offset = 0;

    while (offset < length)
    {
        long sent = tls ? ryazan_tls_send(socket, data + offset, length - offset)
                        : ussr_socket_send(socket, data + offset, length - offset);

        if (sent > 0)
        {
            offset += (size_t)sent;
            continue;
        }
        if (sent < 0 && (tls || !ussr_socket_would_block()))
        {
            snprintf(error, error_size, "send failed: %s",
                     tls ? ryazan_last_error() : ussr_socket_last_error());
            return -1;
        }
        if (rz_wait(socket, 1, deadline) != 0)
        {
            snprintf(error, error_size, "timed out sending the request");
            return -1;
        }
    }
    while (tls)
    {
        int flushed = ryazan_tls_flush(socket);

        if (flushed == RYAZAN_DONE)
            break;
        if (flushed == RYAZAN_ERROR || rz_wait(socket, 1, deadline) != 0)
        {
            snprintf(error, error_size, "send failed: %s", ryazan_last_error());
            return -1;
        }
    }
    return 0;
}

int ryazan_http_request(const char *method, const char *url,
                        const char *headers, const char *body,
                        size_t body_length, int timeout_seconds,
                        ryazan_response_t *response,
                        char *error, size_t error_size)
{
    rz_url_t parts;
    rz_buf_t request = { NULL, 0, 0 };
    rz_buf_t raw = { NULL, 0, 0 };
    time_t deadline;
    int socket = -1;
    int tls = 0;
    int result = -1;
    char line[512];

    memset(response, 0, sizeof(*response));
    error[0] = '\0';
    if (method == NULL || method[0] == '\0' || strpbrk(method, " \r\n") != NULL)
    {
        snprintf(error, error_size, "bad HTTP method");
        return -1;
    }
    if (url == NULL || rz_parse_url(url, &parts, error, error_size) != 0)
        return -1;
    if (headers != NULL && headers[0] != '\0' &&
        (strlen(headers) < 2 || strcmp(headers + strlen(headers) - 2, "\r\n") != 0))
    {
        snprintf(error, error_size, "each header line must end in \\r\\n");
        return -1;
    }
    if (parts.https && strcmp(rz_be_name(), "none") == 0)
    {
        snprintf(error, error_size, "this ussr was built without TLS; https:// is unavailable");
        return -1;
    }
    if (timeout_seconds <= 0)
        timeout_seconds = 60;
    deadline = time(NULL) + timeout_seconds;

    if (ussr_socket_init() != 0 || (socket = ussr_socket_tcp()) < 1)
    {
        snprintf(error, error_size, "cannot create a socket: %s", ussr_socket_last_error());
        return -1;
    }
    if (ussr_socket_connect(socket, parts.host, parts.port,
                            timeout_seconds * 1000) != 0)
    {
        snprintf(error, error_size, "cannot connect to %s:%u: %s", parts.host,
                 (unsigned)parts.port, ussr_socket_last_error());
        goto done;
    }
    if (parts.https)
    {
        int state;

        if (ryazan_tls_client(socket, parts.host) != 0)
        {
            snprintf(error, error_size, "%s", ryazan_last_error());
            goto done;
        }
        tls = 1;
        while ((state = ryazan_tls_handshake(socket)) != RYAZAN_DONE)
        {
            if (state == RYAZAN_ERROR ||
                rz_wait(socket, state == RYAZAN_WANT_WRITE, deadline) != 0)
            {
                snprintf(error, error_size, "TLS handshake with %s failed: %s",
                         parts.host, state == RYAZAN_ERROR ? ryazan_last_error() : "timed out");
                goto done;
            }
        }
    }

    /* the target keeps its query; a bare "?q" or "" gets the "/" path */
    if (rz_append(&request, method, strlen(method)) != 0 ||
        rz_append(&request, " ", 1) != 0 ||
        (parts.target[0] != '/' && rz_append(&request, "/", 1) != 0) ||
        rz_append(&request, parts.target, strcspn(parts.target, "#")) != 0 ||
        rz_append(&request, " HTTP/1.1\r\nHost: ", 17) != 0 ||
        rz_append(&request, parts.host, strlen(parts.host)) != 0)
        goto oom;
    if ((parts.https && parts.port != 443) || (!parts.https && parts.port != 80))
    {
        snprintf(line, sizeof(line), ":%u", (unsigned)parts.port);
        if (rz_append(&request, line, strlen(line)) != 0)
            goto oom;
    }
    if (rz_append(&request, "\r\nConnection: close\r\n", 21) != 0)
        goto oom;
    if ((headers == NULL || !rz_has_header(headers, "user-agent")) &&
        rz_append(&request, "User-Agent: ussr-ryazan\r\n", 25) != 0)
        goto oom;
    if ((headers == NULL || !rz_has_header(headers, "accept")) &&
        rz_append(&request, "Accept: */*\r\n", 13) != 0)
        goto oom;
    if (headers != NULL && rz_append(&request, headers, strlen(headers)) != 0)
        goto oom;
    if ((body != NULL || strcmp(method, "POST") == 0 || strcmp(method, "PUT") == 0) &&
        (headers == NULL || !rz_has_header(headers, "content-length")))
    {
        snprintf(line, sizeof(line), "Content-Length: %lu\r\n",
                 (unsigned long)(body != NULL ? body_length : 0));
        if (rz_append(&request, line, strlen(line)) != 0)
            goto oom;
    }
    if (rz_append(&request, "\r\n", 2) != 0 ||
        (body != NULL && rz_append(&request, body, body_length) != 0))
        goto oom;

    if (rz_send_all(socket, tls, (const char *)request.data, request.length,
                    deadline, error, error_size) != 0)
        goto done;

    for (;;)
    {
        long got;

        if (rz_reserve(&raw, RZ_READ_CHUNK) != 0)
            goto oom;
        got = tls ? ryazan_tls_recv(socket, raw.data + raw.length, RZ_READ_CHUNK)
                  : ussr_socket_recv(socket, raw.data + raw.length, RZ_READ_CHUNK);
        if (got > 0)
        {
            raw.length += (size_t)got;
            raw.data[raw.length] = '\0';
            if (raw.length > RZ_MAX_RESPONSE)
            {
                snprintf(error, error_size, "response from %s is too large", parts.host);
                goto done;
            }
            if (rz_response_complete(&raw))
                break;
            continue;
        }
        if (got == 0)
            break;
        if (tls ? got != RYAZAN_RECV_WANT_READ : !ussr_socket_would_block())
        {
            snprintf(error, error_size, "receive failed: %s",
                     tls ? ryazan_last_error() : ussr_socket_last_error());
            goto done;
        }
        if (rz_wait(socket, 0, deadline) != 0)
        {
            snprintf(error, error_size, "timed out waiting for %s", parts.host);
            goto done;
        }
    }

    {
        char *end = raw.data ? strstr((char *)raw.data, "\r\n\r\n") : NULL;
        size_t head_length;
        size_t body_size;
        char *value;

        if (end == NULL || strncmp((char *)raw.data, "HTTP/1.", 7) != 0)
        {
            snprintf(error, error_size, "%s sent no valid HTTP response", parts.host);
            goto done;
        }
        head_length = (size_t)(end - (char *)raw.data) + 2;
        response->head = malloc(head_length + 1);
        if (response->head == NULL)
            goto oom;
        memcpy(response->head, raw.data, head_length);
        response->head[head_length] = '\0';
        response->status = strtol((char *)raw.data + 9, NULL, 10);

        body_size = raw.length - (head_length + 2);
        memmove(raw.data, raw.data + head_length + 2, body_size);
        value = rz_header_value(response->head, "transfer-encoding");
        if (value != NULL && strstr(value, "chunked") != NULL)
        {
            long decoded = rz_dechunk(raw.data, body_size);

            if (decoded < 0)
            {
                free(value);
                snprintf(error, error_size, "%s sent a malformed chunked body", parts.host);
                goto done;
            }
            body_size = (size_t)decoded;
        }
        else
        {
            char *length_text = rz_header_value(response->head, "content-length");

            if (length_text != NULL)
            {
                size_t declared = (size_t)strtoul(length_text, NULL, 10);

                free(length_text);
                if (declared > body_size)
                {
                    free(value);
                    snprintf(error, error_size, "%s closed the connection mid-response", parts.host);
                    goto done;
                }
                body_size = declared;
            }
        }
        free(value);
        raw.data[body_size] = '\0';
        response->body = (char *)raw.data;
        response->body_length = body_size;
        raw.data = NULL;
        raw.length = raw.capacity = 0;
    }
    result = 0;
    goto done;

oom:
    snprintf(error, error_size, "out of memory");
done:
    if (result != 0)
        ryazan_response_free(response);
    if (tls)
        (void)ryazan_tls_close(socket);
    if (socket > 0)
        (void)ussr_socket_close(socket);
    rz_buf_free(&request);     /* may hold an API key */
    rz_buf_free(&raw);
    return result;
}

void ryazan_response_free(ryazan_response_t *response)
{
    if (response == NULL)
        return;
    free(response->head);
    free(response->body);
    memset(response, 0, sizeof(*response));
}

/* ------------------------------------------------------------- */
/* the "none" backend                                               */
/* ------------------------------------------------------------- */

#ifdef RYAZAN_BACKEND_NONE

const char *rz_be_name(void) { return "none"; }

int rz_be_client(rz_conn_t *conn)
{
    (void)conn;
    rz_fail("this ussr was built without TLS (no Schannel or OpenSSL)");
    return RZ_FAIL;
}

int rz_be_server(rz_conn_t *conn, const char *credential, const char *secret)
{
    (void)credential;
    (void)secret;
    return rz_be_client(conn);
}

int rz_be_handshake(rz_conn_t *conn) { (void)conn; return RZ_FAIL; }
int rz_be_decrypt(rz_conn_t *conn) { (void)conn; return RZ_FAIL; }

int rz_be_encrypt(rz_conn_t *conn, const void *data, size_t length)
{
    (void)conn;
    (void)data;
    (void)length;
    return RZ_FAIL;
}

void rz_be_shutdown(rz_conn_t *conn) { (void)conn; }

void rz_be_info(rz_conn_t *conn, char *text, size_t size)
{
    (void)conn;
    if (size > 0)
        text[0] = '\0';
}
void rz_be_free(rz_conn_t *conn) { (void)conn; }
void rz_be_cleanup(void) { }

#endif
