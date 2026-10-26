#ifndef USSR_SOCKET_H
#define USSR_SOCKET_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Stable, small integer handles are used by USSR scripts. Native SOCKET/fd
 * values never cross the language boundary (important on Win64). */
typedef int ussr_socket_handle_t;

enum { USSR_SOCKET_READ = 1, USSR_SOCKET_WRITE = 2 };
typedef struct {
    ussr_socket_handle_t handle;
    short events;   /* USSR_SOCKET_READ / USSR_SOCKET_WRITE */
    short revents;  /* same flags; 0 when not ready, -1 on invalid handle */
} ussr_socket_pollfd_t;

int ussr_socket_init(void);
void ussr_socket_cleanup(void);
int ussr_socket_tcp(void);
int ussr_socket_bind(ussr_socket_handle_t handle, const char *ipv4, unsigned short port);
int ussr_socket_listen(ussr_socket_handle_t handle, int backlog);
int ussr_socket_accept(ussr_socket_handle_t handle);
int ussr_socket_nonblocking(ussr_socket_handle_t handle);
/* recv returns bytes read, 0 for orderly peer close, -1 for would-block/error. */
long ussr_socket_recv(ussr_socket_handle_t handle, void *buffer, size_t capacity);
/* send returns bytes written, or -1 for would-block/error. */
long ussr_socket_send(ussr_socket_handle_t handle, const void *buffer, size_t length);
int ussr_socket_close(ussr_socket_handle_t handle);
/* Connects to host (an IPv4 address or a DNS name) and port, waiting at
 * most timeout_ms. Returns 0, or -1 with ussr_socket_last_error set. The
 * socket stays nonblocking. */
int ussr_socket_connect(ussr_socket_handle_t handle, const char *host,
                        unsigned short port, int timeout_ms);
/* 1 when the last failed recv/send only would have blocked, 0 when it
 * was a real error (reset, invalid handle, ...). */
int ussr_socket_would_block(void);
/* Portable readiness wait. Returns ready count, 0 timeout, or -1 on error. */
int ussr_socket_poll(ussr_socket_pollfd_t *fds, size_t count, int timeout_ms);
const char *ussr_socket_last_error(void);

#ifdef __cplusplus
}
#endif
#endif
