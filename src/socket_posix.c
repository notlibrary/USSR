#define _POSIX_C_SOURCE 200112L
#include "socket.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>

#define SOCKET_SLOTS 4096
static int slots[SOCKET_SLOTS];
static int initialized;
static char error_text[192];
static int last_would_block;
static void set_error(const char *what) { snprintf(error_text, sizeof(error_text), "%s: %s", what, strerror(errno)); }
const char *ussr_socket_last_error(void) { return error_text[0] ? error_text : "no socket error"; }
int ussr_socket_init(void) { if (!initialized) { for (int i=0;i<SOCKET_SLOTS;i++) slots[i]=-1; initialized=1; } error_text[0]=0; return 0; }
void ussr_socket_cleanup(void) { if (!initialized) return; for (int i=0;i<SOCKET_SLOTS;i++) if (slots[i]>=0) close(slots[i]); for (int i=0;i<SOCKET_SLOTS;i++) slots[i]=-1; initialized=0; }
static int put_socket(int fd) { for (int i=0;i<SOCKET_SLOTS;i++) if (slots[i]<0) { slots[i]=fd; return i+1; } errno=EMFILE; set_error("socket table full"); close(fd); return -1; }
static int get_socket(int h) { if (h<1 || h>SOCKET_SLOTS || slots[h-1]<0) { errno=EBADF; set_error("invalid socket handle"); return -1; } return slots[h-1]; }
int ussr_socket_tcp(void) { int fd, h, flags; if (ussr_socket_init()!=0) return -1; fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0){set_error("socket");return -1;} flags=fcntl(fd,F_GETFL,0); if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){set_error("nonblocking");close(fd);return -1;} h=put_socket(fd); return h; }
int ussr_socket_bind(int h,const char *ip,unsigned short port) { int fd=get_socket(h), yes=1; struct sockaddr_in a; if(fd<0)return -1; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons(port); if(!ip || !strcmp(ip,"*") || !strcmp(ip,"0.0.0.0")) a.sin_addr.s_addr=htonl(INADDR_ANY); else if(inet_pton(AF_INET,ip,&a.sin_addr)!=1){errno=EINVAL;set_error("invalid IPv4 address");return -1;} (void)setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)); if(bind(fd,(struct sockaddr*)&a,sizeof(a))<0){set_error("bind");return -1;} return 0; }
int ussr_socket_listen(int h,int backlog){int fd=get_socket(h);if(fd<0)return -1;if(backlog<1)backlog=1;if(backlog>4096)backlog=4096;if(listen(fd,backlog)<0){set_error("listen");return -1;}return 0;}
int ussr_socket_accept(int h){int fd=get_socket(h),c;if(fd<0)return -1;c=accept(fd,NULL,NULL);if(c<0){set_error("accept");return -1;}int flags=fcntl(c,F_GETFL,0);if(flags<0||fcntl(c,F_SETFL,flags|O_NONBLOCK)<0){set_error("set accepted socket nonblocking");close(c);return -1;}return put_socket(c);}
int ussr_socket_nonblocking(int h){int fd=get_socket(h),flags;if(fd<0)return -1;flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){set_error("nonblocking");return -1;}return 0;}
long ussr_socket_recv(int h,void *b,size_t n){int fd=get_socket(h);ssize_t r;last_would_block=0;if(fd<0)return -1;r=recv(fd,b,n,0);if(r<0){last_would_block=(errno==EAGAIN||errno==EWOULDBLOCK);set_error((errno==EAGAIN||errno==EWOULDBLOCK)?"would block":"recv");return -1;}return (long)r;}
long ussr_socket_send(int h,const void *b,size_t n){int fd=get_socket(h);ssize_t r;last_would_block=0;if(fd<0)return -1;
#ifdef MSG_NOSIGNAL
r=send(fd,b,n,MSG_NOSIGNAL);
#else
r=send(fd,b,n,0);
#endif
if(r<0) last_would_block=(errno==EAGAIN||errno==EWOULDBLOCK);
if(r<0) set_error((errno==EAGAIN||errno==EWOULDBLOCK)?"would block":"send");
return (long)r;
}
int ussr_socket_close(int h){int fd=get_socket(h);if(fd<0)return -1;slots[h-1]=-1;if(close(fd)<0){set_error("close");return -1;}return 0;}
int ussr_socket_poll(ussr_socket_pollfd_t *fds,size_t n,int timeout){struct pollfd *p;int r;if(n && !fds)return -1;p=calloc(n?n:1,sizeof(*p));if(!p)return -1;for(size_t i=0;i<n;i++){int fd=get_socket(fds[i].handle);fds[i].revents=0;if(fd<0){p[i].fd=-1;fds[i].revents=-1;continue;}p[i].fd=fd;p[i].events=(short)(((fds[i].events&USSR_SOCKET_READ)?POLLIN:0)|((fds[i].events&USSR_SOCKET_WRITE)?POLLOUT:0));}r=poll(p,(nfds_t)n,timeout);if(r>=0)for(size_t i=0;i<n;i++){if(fds[i].revents==-1)continue;if(p[i].revents&(POLLIN|POLLHUP|POLLERR|POLLNVAL))fds[i].revents|=USSR_SOCKET_READ;if(p[i].revents&(POLLOUT|POLLERR|POLLHUP|POLLNVAL))fds[i].revents|=USSR_SOCKET_WRITE;}else set_error("poll");free(p);return r;}
int ussr_socket_would_block(void){return last_would_block;}
int ussr_socket_connect(int h,const char *host,unsigned short port,int timeout_ms){
    int fd=get_socket(h),r,e=0;struct addrinfo hints,*res=NULL;char service[8];struct pollfd p;socklen_t len=sizeof(e);
    if(fd<0)return -1;
    memset(&hints,0,sizeof(hints));hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    snprintf(service,sizeof(service),"%u",(unsigned)port);
    if(!host||!*host||getaddrinfo(host,service,&hints,&res)!=0||!res){snprintf(error_text,sizeof(error_text),"cannot resolve %.120s",host?host:"");return -1;}
    r=connect(fd,res->ai_addr,res->ai_addrlen);freeaddrinfo(res);
    if(r==0)return 0;
    if(errno!=EINPROGRESS){set_error("connect");return -1;}
    p.fd=fd;p.events=POLLOUT;p.revents=0;
    r=poll(&p,1,timeout_ms);
    if(r==0){errno=ETIMEDOUT;set_error("connect");return -1;}
    if(r<0||getsockopt(fd,SOL_SOCKET,SO_ERROR,&e,&len)<0){set_error("connect");return -1;}
    if(e!=0){errno=e;set_error("connect");return -1;}
    return 0;
}

int ussr_socket_udp(const char *ip,unsigned short port){
    int fd,h,yes=1,flags;struct sockaddr_in a;
    if(ussr_socket_init()!=0)return -1;
    memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(port);
    if(!ip||!*ip||!strcmp(ip,"*")||!strcmp(ip,"0.0.0.0"))a.sin_addr.s_addr=htonl(INADDR_ANY);
    else if(inet_pton(AF_INET,ip,&a.sin_addr)!=1){errno=EINVAL;set_error("invalid IPv4 address");return -1;}
    fd=socket(AF_INET,SOCK_DGRAM,0);if(fd<0){set_error("socket");return -1;}
    (void)setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes));
    /* no SO_REUSEADDR: a second program asking for the same port fails */
    if(bind(fd,(struct sockaddr*)&a,sizeof(a))<0){set_error("bind");close(fd);return -1;}
    flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){set_error("nonblocking");close(fd);return -1;}
    h=put_socket(fd);return h;
}
long ussr_socket_sendto(int h,const char *host,unsigned short port,const void *b,size_t n){
    int fd=get_socket(h);struct addrinfo hints,*res=NULL;char service[8];ssize_t r;
    if(fd<0)return -1;
    memset(&hints,0,sizeof(hints));hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
    snprintf(service,sizeof(service),"%u",(unsigned)port);
    if(!host||!*host||getaddrinfo(host,service,&hints,&res)!=0||!res){snprintf(error_text,sizeof(error_text),"cannot resolve %.120s",host?host:"");return -1;}
    r=sendto(fd,b,n,0,res->ai_addr,res->ai_addrlen);freeaddrinfo(res);
    if(r<0){set_error("sendto");return -1;}
    return (long)r;
}
long ussr_socket_recvfrom(int h,void *b,size_t n,int timeout_ms,char *ip,size_t ipcap,unsigned *port){
    int fd=get_socket(h);struct sockaddr_in a;socklen_t alen;ssize_t r;struct pollfd p;
    if(fd<0)return -1;
    for(;;){
        alen=sizeof(a);
        r=recvfrom(fd,b,n,0,(struct sockaddr*)&a,&alen);
        if(r>=0)break;
        if(errno==EINTR||errno==ECONNREFUSED)continue;
        if(errno!=EAGAIN&&errno!=EWOULDBLOCK){set_error("recvfrom");return -1;}
        if(timeout_ms<=0)return -2;
        p.fd=fd;p.events=POLLIN;p.revents=0;
        r=poll(&p,1,timeout_ms);
        if(r<0&&errno!=EINTR){set_error("poll");return -1;}
        if(r==0)return -2;
        timeout_ms=0;
    }
    if(ip&&ipcap){if(!inet_ntop(AF_INET,&a.sin_addr,ip,(socklen_t)ipcap))ip[0]=0;}
    if(port)*port=ntohs(a.sin_port);
    return (long)r;
}
