#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include "socket.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#define SOCKET_SLOTS 4096
static SOCKET slots[SOCKET_SLOTS];
static int initialized;
static char error_text[192];
static int last_would_block;
static void set_error(const char *what) { snprintf(error_text,sizeof(error_text),"%s: Winsock error %d",what,WSAGetLastError()); }
const char *ussr_socket_last_error(void){return error_text[0]?error_text:"no socket error";}
int ussr_socket_init(void){if(!initialized){WSADATA d;if(WSAStartup(MAKEWORD(2,2),&d)!=0){set_error("WSAStartup");return -1;}for(int i=0;i<SOCKET_SLOTS;i++)slots[i]=INVALID_SOCKET;initialized=1;}error_text[0]=0;return 0;}
void ussr_socket_cleanup(void){if(!initialized)return;for(int i=0;i<SOCKET_SLOTS;i++)if(slots[i]!=INVALID_SOCKET)closesocket(slots[i]);for(int i=0;i<SOCKET_SLOTS;i++)slots[i]=INVALID_SOCKET;initialized=0;WSACleanup();}
static int put_socket(SOCKET s){for(int i=0;i<SOCKET_SLOTS;i++)if(slots[i]==INVALID_SOCKET){slots[i]=s;return i+1;}WSASetLastError(WSAEMFILE);set_error("socket table full");closesocket(s);return -1;}
static SOCKET get_socket(int h){if(h<1||h>SOCKET_SLOTS||slots[h-1]==INVALID_SOCKET){WSASetLastError(WSAENOTSOCK);set_error("invalid socket handle");return INVALID_SOCKET;}return slots[h-1];}
int ussr_socket_tcp(void){SOCKET s;if(ussr_socket_init()!=0)return -1;s=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);if(s==INVALID_SOCKET){set_error("socket");return -1;}int h=put_socket(s);if(h<0)return -1;if(ussr_socket_nonblocking(h)!=0){ussr_socket_close(h);return -1;}return h;}
int ussr_socket_bind(int h,const char *ip,unsigned short port){SOCKET s=get_socket(h);struct sockaddr_in a;if(s==INVALID_SOCKET)return -1;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(port);if(!ip||!strcmp(ip,"*")||!strcmp(ip,"0.0.0.0"))a.sin_addr.s_addr=htonl(INADDR_ANY);else if(InetPtonA(AF_INET,ip,&a.sin_addr)!=1){WSASetLastError(WSAEINVAL);set_error("invalid IPv4 address");return -1;}BOOL yes=TRUE;(void)setsockopt(s,SOL_SOCKET,SO_REUSEADDR,(const char*)&yes,sizeof(yes));if(bind(s,(struct sockaddr*)&a,sizeof(a))==SOCKET_ERROR){set_error("bind");return -1;}return 0;}
int ussr_socket_listen(int h,int backlog){SOCKET s=get_socket(h);if(s==INVALID_SOCKET)return -1;if(backlog<1)backlog=1;if(backlog>4096)backlog=4096;if(listen(s,backlog)==SOCKET_ERROR){set_error("listen");return -1;}return 0;}
int ussr_socket_accept(int h){SOCKET s=get_socket(h),c;if(s==INVALID_SOCKET)return -1;c=accept(s,NULL,NULL);if(c==INVALID_SOCKET){set_error("accept");return -1;}int r=put_socket(c);if(r<0)return -1;if(ussr_socket_nonblocking(r)!=0){ussr_socket_close(r);return -1;}return r;}
int ussr_socket_nonblocking(int h){SOCKET s=get_socket(h);u_long one=1;if(s==INVALID_SOCKET)return -1;if(ioctlsocket(s,FIONBIO,&one)==SOCKET_ERROR){set_error("nonblocking");return -1;}return 0;}
long ussr_socket_recv(int h,void *b,size_t n){SOCKET s=get_socket(h);int r;last_would_block=0;if(s==INVALID_SOCKET)return -1;if(n>INT_MAX)n=INT_MAX;r=recv(s,(char*)b,(int)n,0);if(r==SOCKET_ERROR){last_would_block=WSAGetLastError()==WSAEWOULDBLOCK;set_error("recv/would block");return -1;}return r;}
long ussr_socket_send(int h,const void *b,size_t n){SOCKET s=get_socket(h);int r;last_would_block=0;if(s==INVALID_SOCKET)return -1;if(n>INT_MAX)n=INT_MAX;r=send(s,(const char*)b,(int)n,0);if(r==SOCKET_ERROR){last_would_block=WSAGetLastError()==WSAEWOULDBLOCK;set_error("send/would block");}return r==SOCKET_ERROR?-1:r;}
int ussr_socket_close(int h){SOCKET s=get_socket(h);if(s==INVALID_SOCKET)return -1;slots[h-1]=INVALID_SOCKET;if(closesocket(s)==SOCKET_ERROR){set_error("close");return -1;}return 0;}
int ussr_socket_poll(ussr_socket_pollfd_t *fds,size_t n,int timeout){WSAPOLLFD *p;int r;if(n&&!fds)return -1;p=calloc(n?n:1,sizeof(*p));if(!p)return -1;for(size_t i=0;i<n;i++){SOCKET s=get_socket(fds[i].handle);fds[i].revents=0;if(s==INVALID_SOCKET){fds[i].revents=-1;p[i].fd=INVALID_SOCKET;continue;}p[i].fd=s;p[i].events=(SHORT)(((fds[i].events&USSR_SOCKET_READ)?POLLRDNORM:0)|((fds[i].events&USSR_SOCKET_WRITE)?POLLWRNORM:0));}r=WSAPoll(p,(ULONG)n,timeout);if(r!=SOCKET_ERROR)for(size_t i=0;i<n;i++){if(fds[i].revents==-1)continue;if(p[i].revents&(POLLRDNORM|POLLHUP|POLLERR|POLLNVAL))fds[i].revents|=USSR_SOCKET_READ;if(p[i].revents&(POLLWRNORM|POLLHUP|POLLERR|POLLNVAL))fds[i].revents|=USSR_SOCKET_WRITE;}else set_error("WSAPoll");free(p);return r==SOCKET_ERROR?-1:r;}
int ussr_socket_would_block(void){return last_would_block;}
int ussr_socket_connect(int h,const char *host,unsigned short port,int timeout_ms){
    SOCKET s=get_socket(h);ADDRINFOA hints,*res=NULL;char service[8];WSAPOLLFD p;int r,e=0,len=sizeof(e);
    if(s==INVALID_SOCKET)return -1;
    memset(&hints,0,sizeof(hints));hints.ai_family=AF_INET;hints.ai_socktype=SOCK_STREAM;
    snprintf(service,sizeof(service),"%u",(unsigned)port);
    if(!host||!*host||getaddrinfo(host,service,&hints,&res)!=0||!res){snprintf(error_text,sizeof(error_text),"cannot resolve %.120s",host?host:"");return -1;}
    r=connect(s,res->ai_addr,(int)res->ai_addrlen);freeaddrinfo(res);
    if(r==0)return 0;
    if(WSAGetLastError()!=WSAEWOULDBLOCK){set_error("connect");return -1;}
    p.fd=s;p.events=POLLWRNORM;p.revents=0;
    r=WSAPoll(&p,1,timeout_ms);
    if(r==0){WSASetLastError(WSAETIMEDOUT);set_error("connect");return -1;}
    if(r==SOCKET_ERROR){set_error("connect");return -1;}
    if(getsockopt(s,SOL_SOCKET,SO_ERROR,(char*)&e,&len)==SOCKET_ERROR){set_error("connect");return -1;}
    if(e!=0){WSASetLastError(e);set_error("connect");return -1;}
    return 0;
}

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR,12)
#endif
int ussr_socket_udp(const char *ip,unsigned short port){
    SOCKET s;struct sockaddr_in a;BOOL yes=TRUE,no=FALSE;DWORD got=0;int h;
    if(ussr_socket_init()!=0)return -1;
    memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(port);
    if(!ip||!*ip||!strcmp(ip,"*")||!strcmp(ip,"0.0.0.0"))a.sin_addr.s_addr=htonl(INADDR_ANY);
    else if(InetPtonA(AF_INET,ip,&a.sin_addr)!=1){WSASetLastError(WSAEINVAL);set_error("invalid IPv4 address");return -1;}
    s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET){set_error("socket");return -1;}
    (void)setsockopt(s,SOL_SOCKET,SO_BROADCAST,(const char*)&yes,sizeof(yes));
    if(port!=0)(void)setsockopt(s,SOL_SOCKET,SO_EXCLUSIVEADDRUSE,(const char*)&yes,sizeof(yes));
    /* an ICMP "port unreachable" must not fail the next recvfrom */
    (void)WSAIoctl(s,SIO_UDP_CONNRESET,&no,sizeof(no),NULL,0,&got,NULL,NULL);
    if(bind(s,(struct sockaddr*)&a,sizeof(a))==SOCKET_ERROR){set_error("bind");closesocket(s);return -1;}
    h=put_socket(s);if(h<0)return -1;
    if(ussr_socket_nonblocking(h)!=0){ussr_socket_close(h);return -1;}
    return h;
}
long ussr_socket_sendto(int h,const char *host,unsigned short port,const void *b,size_t n){
    SOCKET s=get_socket(h);ADDRINFOA hints,*res=NULL;char service[8];int r;
    if(s==INVALID_SOCKET)return -1;
    memset(&hints,0,sizeof(hints));hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
    snprintf(service,sizeof(service),"%u",(unsigned)port);
    if(!host||!*host||getaddrinfo(host,service,&hints,&res)!=0||!res){snprintf(error_text,sizeof(error_text),"cannot resolve %.120s",host?host:"");return -1;}
    if(n>INT_MAX)n=INT_MAX;
    r=sendto(s,(const char*)b,(int)n,0,res->ai_addr,(int)res->ai_addrlen);freeaddrinfo(res);
    if(r==SOCKET_ERROR){set_error("sendto");return -1;}
    return r;
}
long ussr_socket_recvfrom(int h,void *b,size_t n,int timeout_ms,char *ip,size_t ipcap,unsigned *port){
    SOCKET s=get_socket(h);struct sockaddr_in a;int alen,r;WSAPOLLFD p;
    if(s==INVALID_SOCKET)return -1;
    if(n>INT_MAX)n=INT_MAX;
    for(;;){
        alen=sizeof(a);
        r=recvfrom(s,(char*)b,(int)n,0,(struct sockaddr*)&a,&alen);
        if(r!=SOCKET_ERROR)break;
        r=WSAGetLastError();
        if(r==WSAECONNRESET)continue;
        if(r==WSAEMSGSIZE){r=(int)n;break;}
        if(r!=WSAEWOULDBLOCK){set_error("recvfrom");return -1;}
        if(timeout_ms<=0)return -2;
        p.fd=s;p.events=POLLRDNORM;p.revents=0;
        r=WSAPoll(&p,1,timeout_ms);
        if(r==SOCKET_ERROR){set_error("WSAPoll");return -1;}
        if(r==0)return -2;
        timeout_ms=0;
    }
    if(ip&&ipcap){if(!InetNtopA(AF_INET,&a.sin_addr,ip,ipcap))ip[0]=0;}
    if(port)*port=ntohs(a.sin_port);
    return r;
}
