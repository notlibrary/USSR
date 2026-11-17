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
/* fwrite/fread/memcpy between typed objects and bytes (see p_bytecall) */
size_t __fwrite_t(const void *p, size_t size, size_t n, void *f, __uval desc, long stride);
size_t __fread_t(void *p, size_t size, size_t n, void *f, __uval desc, long stride);
void *__memcpy_t(void *d, const void *s, size_t n, __uval ddesc, long dstride, __uval sdesc, long sstride);
void __ginit(void);
void abort(void);
#define __builtin_expect(x, v) (x)
#define __builtin_trap() abort()
#define __builtin_abort() abort()
#define __builtin_unreachable() abort()
/* GCC's bit builtins (libc.c) and the ones that are only names */
int __builtin_popcount(unsigned int x);
int __builtin_popcountl(unsigned long x);
int __builtin_popcountll(unsigned long long x);
int __builtin_clz(unsigned int x);
int __builtin_clzl(unsigned long x);
int __builtin_clzll(unsigned long long x);
int __builtin_ctz(unsigned int x);
int __builtin_ctzl(unsigned long x);
int __builtin_ctzll(unsigned long long x);
int __builtin_ffs(int x);
int __builtin_ffsl(long x);
int __builtin_ffsll(long long x);
int __builtin_parity(unsigned int x);
int __builtin_parityl(unsigned long x);
int __builtin_parityll(unsigned long long x);
unsigned short __builtin_bswap16(unsigned short x);
unsigned int __builtin_bswap32(unsigned int x);
unsigned long __builtin_bswap64(unsigned long x);
#define __builtin_offsetof(type, member) ((size_t)&((type *)0)->member)
#define __builtin_constant_p(x) 0
#define __builtin_assume_aligned(p, ...) (p)
#define __builtin_prefetch(...) ((void)0)
#define __builtin_memcpy memcpy
#define __builtin_memset memset
int __start(long argc, __uval argv);
int main();
#endif
