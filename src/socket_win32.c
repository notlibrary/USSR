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
long ussr_socket_recv(int h,void *b,size_t n){SOCKET s=get_socket(h);int r;if(s==INVALID_SOCKET)return -1;if(n>INT_MAX)n=INT_MAX;r=recv(s,(char*)b,(int)n,0);if(r==SOCKET_ERROR){set_error("recv/would block");return -1;}return r;}
long ussr_socket_send(int h,const void *b,size_t n){SOCKET s=get_socket(h);int r;if(s==INVALID_SOCKET)return -1;if(n>INT_MAX)n=INT_MAX;r=send(s,(const char*)b,(int)n,0);if(r==SOCKET_ERROR)set_error("send/would block");return r==SOCKET_ERROR?-1:r;}
int ussr_socket_close(int h){SOCKET s=get_socket(h);if(s==INVALID_SOCKET)return -1;slots[h-1]=INVALID_SOCKET;if(closesocket(s)==SOCKET_ERROR){set_error("close");return -1;}return 0;}
int ussr_socket_poll(ussr_socket_pollfd_t *fds,size_t n,int timeout){WSAPOLLFD *p;int r;if(n&&!fds)return -1;p=calloc(n?n:1,sizeof(*p));if(!p)return -1;for(size_t i=0;i<n;i++){SOCKET s=get_socket(fds[i].handle);fds[i].revents=0;if(s==INVALID_SOCKET){fds[i].revents=-1;p[i].fd=INVALID_SOCKET;continue;}p[i].fd=s;p[i].events=(SHORT)(((fds[i].events&USSR_SOCKET_READ)?POLLRDNORM:0)|((fds[i].events&USSR_SOCKET_WRITE)?POLLWRNORM:0));}r=WSAPoll(p,(ULONG)n,timeout);if(r!=SOCKET_ERROR)for(size_t i=0;i<n;i++){if(fds[i].revents==-1)continue;if(p[i].revents&(POLLRDNORM|POLLHUP|POLLERR|POLLNVAL))fds[i].revents|=USSR_SOCKET_READ;if(p[i].revents&(POLLWRNORM|POLLHUP|POLLERR|POLLNVAL))fds[i].revents|=USSR_SOCKET_WRITE;}else set_error("WSAPoll");free(p);return r==SOCKET_ERROR?-1:r;}
