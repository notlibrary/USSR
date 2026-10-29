/* usa prelude: read before every file. The runtime functions the code
   generator calls by name, and the builtins libc uses.
   __uval holds a USSR value as it is (a string, a vector); a function
   called __ussr_NAME is the USSR builtin NAME. */
#ifndef __USA_H
#define __USA_H
typedef unsigned long size_t;
typedef long ptrdiff_t;
typedef char *__builtin_va_list;

long __idiv(long a, long b);
unsigned long __udiv64(unsigned long a, unsigned long b);
unsigned long __umod64(unsigned long a, unsigned long b);
unsigned long __ushr64(unsigned long a, unsigned long n);
long __f2i(double x);
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *p, int c, size_t n);
void __strinit(char *p, __uval s);
void __ginit(void);
int __start(long argc, __uval argv);
int main();
#endif
