/* usa's C library, compiled by usa with every program.

   The VM side: C memory is the USSR vector M (an address indexes it),
   the stack starts after the static data, malloc pushes cells onto M.
   A __uval is a USSR value as it is; __ussr_NAME(...) calls the USSR
   builtin NAME. Files are kept whole in byte vectors: read_bytes when
   opened, write_bytes when flushed. */

#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <setjmp.h>
#include <locale.h>

__uval __ussr_text_bytes(__uval s);
__uval __ussr_clear_bytes(__uval v);
__uval __ussr_concat(__uval a, __uval b);
__uval __ussr_template_str(__uval s);
__uval __ussr_bytes_text(__uval v, __uval from, __uval n);
__uval __ussr_write_stderr(__uval s);
__uval __ussr_push(__uval v, __uval x);
__uval __ussr_vec(void);
__uval __ussr_at(__uval v, __uval i);
__uval __ussr_len(__uval v);
__uval __ussr_byte(__uval s, __uval i);
__uval __ussr_read_bytes(__uval path);
__uval __ussr_write_bytes(__uval path, __uval v);
__uval __ussr_is_dir(__uval path);
__uval __ussr_open_file(__uval path, __uval mode);
__uval __ussr_close_file(__uval h);
__uval __ussr_remove_file(__uval path);
__uval __ussr_rename_path(__uval from, __uval to);
__uval __ussr_getenv(__uval name);
__uval __ussr_terminal_getch(void);
__uval __ussr_sin(__uval x);
__uval __ussr_ln(__uval x);
__uval __ussr_typeof(__uval x);
__uval __mem(void);
void __setmem(__uval v);
long __getsp(void);
void __setsp(long sp);
__uval __getidx(void);
void __setidx(__uval v);
long __data_end(void);
double __rdiv(__uval a, __uval b);
__uval __vget(__uval v, __uval i);
void __vset(__uval v, __uval i, __uval x);
void __exit(int status);
long __time(void);

#define __STACK (1L << 20)

int errno;
static long __brk;
static long __memlen;

/* ---------------- integer and real helpers ---------------- */

long __f2i(double x)
{
    long r, h;
    int neg = 0;
    if (x != x)
        return 0;
    if (x < 0) {
        neg = 1;
        x = -x;
    }
    if (x < 65536) {
        r = (long)__ussr_at(__getidx(), x);
    } else if (x < 4294967296.0) {
        h = (long)__ussr_at(__getidx(), x / 65536);
        r = h * 65536 + (long)__ussr_at(__getidx(), x - h * 65536);
    } else if (x >= 9223372036854775807.0) {
        return neg ? (-9223372036854775807L - 1) : 9223372036854775807L;
    } else {
        h = __f2i(x / 4294967296.0);
        r = h * 4294967296L + __f2i(x - (double)h * 4294967296.0);
    }
    return neg ? -r : r;
}

/* a / b for non-negative a and positive b, bit by bit */
static long __ldiv_slow(long a, long b)
{
    long q = 0, r = 0;
    int i;
    for (i = 62; i >= 0; i--) {
        r = (r << 1) | ((a >> i) & 1);
        if (r >= b) {
            r -= b;
            q |= 1L << i;
        }
    }
    return q;
}

long __idiv(long a, long b)
{
    long r = a % b;
    long q;
    int neg;
    if (a < 4503599627370496L && a > -4503599627370496L)
        return __f2i(__rdiv(a - r, b));
    if (b == -1)
        return -a;
    neg = (a < 0) != (b < 0);
    if (a < 0)
        a = -a;
    if (b < 0)
        b = -b;
    if (a < 0)
        q = __ldiv_slow(-(a + b), b) + 1;
    else
        q = __ldiv_slow(a, b);
    return neg ? -q : q;
}

unsigned long __ushr64(unsigned long a, unsigned long n)
{
    if (n == 0)
        return a;
    return ((long)a >> (long)n) & ((1L << (64 - (long)n)) - 1);
}

unsigned long __udiv64(unsigned long a, unsigned long b)
{
    unsigned long q, r;
    if ((long)a >= 0 && (long)b > 0)
        return __idiv((long)a, (long)b);
    if ((long)b < 0)
        return a >= b ? 1 : 0;
    q = (unsigned long)__idiv((long)(a >> 1), (long)b) << 1;
    r = a - q * b;
    if (r >= b)
        q++;
    return q;
}

unsigned long __umod64(unsigned long a, unsigned long b)
{
    return a - __udiv64(a, b) * b;
}

void __strinit(char *p, __uval s)
{
    long n = (long)__ussr_len(s), i;
    for (i = 0; i < n; i++)
        p[i] = (char)(int)__ussr_byte(s, i);
}

/* ---------------- USSR strings ---------------- */

/* the USSR string of the C string p */
static __uval __ustr(const char *p)
{
    long n = strlen(p);
    if (n == 0)
        return __U("");
    return __ussr_bytes_text(__mem(), (long)p, n);
}

/* a new C string holding the USSR string s */
static char *__cstr(__uval s)
{
    long n = (long)__ussr_len(s), i;
    char *p = malloc(n + 1);
    for (i = 0; i < n; i++)
        p[i] = (char)(int)__ussr_byte(s, i);
    p[n] = 0;
    return p;
}

/* ---------------- memory ---------------- */

/* blocks: a header cell with the size class, then the data; freed blocks
   go to the list of their class */
static long __freel[64];

static void __grow(long need)
{
    __uval m = __mem();
    long chunk = 65536;
    if (need - __memlen > chunk)
        chunk = need - __memlen;
    while (chunk > 0) {
        __ussr_push(m, 0);
        chunk--;
        __memlen++;
    }
}

void *malloc(size_t n)
{
    long size = 16;
    int c = 4;
    long p;
    while (size < (long)n + 8) {
        size = size + size;
        c++;
    }
    if (__freel[c]) {
        p = __freel[c];
        __freel[c] = *(long *)p;
        /* the header held the list link: its class again */
        *(long *)p = c;
        return (void *)(p + 8);
    }
    p = __brk;
    if (p + size > __memlen)
        __grow(p + size);
    __brk = p + size;
    *(long *)p = c;
    return (void *)(p + 8);
}

void free(void *ptr)
{
    long p;
    int c;
    if (!ptr)
        return;
    p = (long)ptr - 8;
    c = (int)*(long *)p;
    if (c < 4 || c > 62) {
        fputs("usa: free() of a pointer malloc did not return\n", stderr);
        abort();
    }
    *(long *)p = __freel[c];
    __freel[c] = p;
}

static long __msize(void *ptr)
{
    long c = *(long *)((long)ptr - 8);
    return (1L << c) - 8;
}

void *calloc(size_t n, size_t size)
{
    void *p = malloc(n * size);
    memset(p, 0, n * size);
    return p;
}

void *realloc(void *ptr, size_t n)
{
    void *q;
    long old;
    if (!ptr)
        return malloc(n);
    old = __msize(ptr);
    if ((long)n <= old)
        return ptr;
    q = malloc(n);
    memcpy(q, ptr, old);
    free(ptr);
    return q;
}

/* ---------------- strings ---------------- */

void *memcpy(void *dst, const void *src, size_t n)
{
    char *d = dst;
    const char *s = src;
    while (n--)
        *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n)
{
    char *d = dst;
    const char *s = src;
    if (d < s) {
        while (n--)
            *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--)
            *--d = *--s;
    }
    return dst;
}

void *memset(void *p, int c, size_t n)
{
    char *d = p;
    while (n--)
        *d++ = c;
    return p;
}

int memcmp(const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    while (n--) {
        if (*x != *y)
            return *x < *y ? -1 : 1;
        x++;
        y++;
    }
    return 0;
}

void *memchr(const void *p, int c, size_t n)
{
    const unsigned char *s = p;
    while (n--) {
        if (*s == (unsigned char)c)
            return (void *)s;
        s++;
    }
    return NULL;
}

size_t strlen(const char *s)
{
    const char *p = s;
    while (*p)
        p++;
    return p - s;
}

size_t strnlen(const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i])
        i++;
    return i;
}

char *strcpy(char *d, const char *s)
{
    char *r = d;
    while ((*d++ = *s++))
        ;
    return r;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && s[i]) {
        d[i] = s[i];
        i++;
    }
    while (i < n)
        d[i++] = 0;
    return d;
}

char *strcat(char *d, const char *s)
{
    strcpy(d + strlen(d), s);
    return d;
}

char *strncat(char *d, const char *s, size_t n)
{
    char *p = d + strlen(d);
    while (n-- && *s)
        *p++ = *s++;
    *p = 0;
    return d;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

int strcasecmp(const char *a, const char *b)
{
    while (*a && tolower(*a) == tolower(*b)) {
        a++;
        b++;
    }
    return tolower(*a) - tolower(*b);
}

int strncasecmp(const char *a, const char *b, size_t n)
{
    while (n && *a && tolower(*a) == tolower(*b)) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return tolower(*a) - tolower(*b);
}

char *strchr(const char *s, int c)
{
    c = (char)c;
    while (*s) {
        if (*s == c)
            return (char *)s;
        s++;
    }
    return c == 0 ? (char *)s : NULL;
}

char *strrchr(const char *s, int c)
{
    const char *r = NULL;
    c = (char)c;
    while (*s) {
        if (*s == c)
            r = s;
        s++;
    }
    return c == 0 ? (char *)s : (char *)r;
}

char *strstr(const char *h, const char *n)
{
    size_t ln = strlen(n);
    if (ln == 0)
        return (char *)h;
    while (*h) {
        if (*h == *n && strncmp(h, n, ln) == 0)
            return (char *)h;
        h++;
    }
    return NULL;
}

size_t strspn(const char *s, const char *accept)
{
    size_t i = 0;
    while (s[i] && strchr(accept, s[i]))
        i++;
    return i;
}

size_t strcspn(const char *s, const char *reject)
{
    size_t i = 0;
    while (s[i] && !strchr(reject, s[i]))
        i++;
    return i;
}

char *strpbrk(const char *s, const char *accept)
{
    while (*s) {
        if (strchr(accept, *s))
            return (char *)s;
        s++;
    }
    return NULL;
}

char *strtok_r(char *s, const char *delim, char **save)
{
    char *e;
    if (!s)
        s = *save;
    s += strspn(s, delim);
    if (!*s) {
        *save = s;
        return NULL;
    }
    e = s + strcspn(s, delim);
    if (*e)
        *e++ = 0;
    *save = e;
    return s;
}

char *strtok(char *s, const char *delim)
{
    static char *save;
    return strtok_r(s, delim, &save);
}

char *strdup(const char *s)
{
    size_t n = strlen(s);
    char *p = malloc(n + 1);
    memcpy(p, s, n + 1);
    return p;
}

char *strndup(const char *s, size_t n)
{
    size_t l = strnlen(s, n);
    char *p = malloc(l + 1);
    memcpy(p, s, l);
    p[l] = 0;
    return p;
}

char *strerror(int e)
{
    switch (e) {
    case 0: return "Success";
    case ENOENT: return "No such file or directory";
    case ENOMEM: return "Cannot allocate memory";
    case EACCES: return "Permission denied";
    case EEXIST: return "File exists";
    case EINVAL: return "Invalid argument";
    case ERANGE: return "Numerical result out of range";
    case EDOM: return "Numerical argument out of domain";
    case EBADF: return "Bad file descriptor";
    case EIO: return "Input/output error";
    }
    return "Unknown error";
}

/* ---------------- ctype ---------------- */

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isblank(int c) { return c == ' ' || c == '\t'; }
int iscntrl(int c) { return (c >= 0 && c < 32) || c == 127; }
int isprint(int c) { return c >= 32 && c < 127; }
int isgraph(int c) { return c > 32 && c < 127; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isascii(int c) { return c >= 0 && c < 128; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }

/* ---------------- files ---------------- */

struct __usa_file {
    int fd;           /* 0 stdin, 1 stdout, 2 stderr, 3 a file */
    int rd, wr;       /* may read, may write */
    int eof, err;
    int dirty;
    int unget;        /* a byte pushed back, or -1 */
    long pos, len;
    __uval data;      /* a file's bytes */
    char *path;
    unsigned char buf[512];  /* stdout/stderr output */
    int blen;
    FILE *next;
};

static FILE __stdin_file, __stdout_file, __stderr_file;
FILE *stdin = &__stdin_file;
FILE *stdout = &__stdout_file;
FILE *stderr = &__stderr_file;
static FILE *__files;
static void (*__atexit_fns[32])(void);
static int __natexit;

/* write len bytes of the buffer of f (stdout or stderr) */
static void __flushout(FILE *f)
{
    int i = 0, j;
    __uval s;
    while (i < f->blen) {
        j = i;
        while (j < f->blen && f->buf[j] != 0)
            j++;
        if (j > i) {
            s = __ussr_bytes_text(__mem(), (long)&f->buf[i], j - i);
            if (f->fd == 1)
                __ussr_template_str(s);
            else
                __ussr_write_stderr(s);
        }
        i = j + 1;
    }
    f->blen = 0;
}

static void __fwritebytes(FILE *f)
{
    if (f->dirty && f->path) {
        if ((long)__ussr_write_bytes(__ustr(f->path), f->data) < 0)
            f->err = 1;
        f->dirty = 0;
    }
}

int fflush(FILE *f)
{
    FILE *g;
    if (!f) {
        fflush(stdout);
        fflush(stderr);
        for (g = __files; g; g = g->next)
            fflush(g);
        return 0;
    }
    if (f->fd == 1 || f->fd == 2)
        __flushout(f);
    else if (f->fd == 3)
        __fwritebytes(f);
    return 0;
}

int fputc(int c, FILE *f)
{
    unsigned char b = c;
    if (f->fd == 1 || f->fd == 2) {
        f->buf[f->blen++] = b;
        if (f->blen == 512 || b == '\n')
            __flushout(f);
        return b;
    }
    if (!f->wr) {
        f->err = 1;
        return EOF;
    }
    if (f->pos < f->len)
        __vset(f->data, f->pos, (long)b);
    else {
        __ussr_push(f->data, (long)b);
        f->len++;
    }
    f->pos++;
    f->dirty = 1;
    return b;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *s, FILE *f)
{
    while (*s)
        fputc(*s++, f);
    return 1;
}

int puts(const char *s)
{
    fputs(s, stdout);
    fputc('\n', stdout);
    return 1;
}

size_t fwrite(const void *p, size_t size, size_t n, FILE *f)
{
    const unsigned char *s = p;
    size_t i, total = size * n;
    for (i = 0; i < total; i++)
        fputc(s[i], f);
    return size ? n : 0;
}

int fgetc(FILE *f)
{
    int c;
    if (f->unget >= 0) {
        c = f->unget;
        f->unget = -1;
        return c;
    }
    if (f->fd == 0) {
        if (f->eof)
            return EOF;
        fflush(stdout);
        c = (int)__ussr_terminal_getch();
        if (c < 0) {
            f->eof = 1;
            return EOF;
        }
        if (c == '\r')
            c = '\n';
        return c;
    }
    if (!f->rd || f->pos >= f->len) {
        f->eof = 1;
        return EOF;
    }
    return (int)__vget(f->data, f->pos++);
}

int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f)
{
    if (c == EOF)
        return EOF;
    f->unget = (unsigned char)c;
    f->eof = 0;
    return c;
}

char *fgets(char *s, int n, FILE *f)
{
    int i = 0, c;
    while (i < n - 1) {
        c = fgetc(f);
        if (c == EOF)
            break;
        s[i++] = c;
        if (c == '\n')
            break;
    }
    if (i == 0)
        return NULL;
    s[i] = 0;
    return s;
}

size_t fread(void *p, size_t size, size_t n, FILE *f)
{
    unsigned char *d = p;
    size_t i, total = size * n;
    int c;
    if (size == 0)
        return 0;
    for (i = 0; i < total; i++) {
        c = fgetc(f);
        if (c == EOF)
            break;
        d[i] = c;
    }
    return i / size;
}

FILE *fopen(const char *path, const char *mode)
{
    FILE *f;
    __uval d;
    int plus = strchr(mode, '+') != NULL;
    d = __ussr_read_bytes(__ustr(path));
    if (mode[0] == 'r' && (long)__ussr_typeof(d) != (long)__ussr_typeof(__ussr_vec())) {
        errno = ENOENT;
        return NULL;
    }
    f = calloc(1, sizeof(FILE));
    f->fd = 3;
    f->unget = -1;
    f->path = strdup(path);
    f->rd = mode[0] == 'r' || plus;
    f->wr = mode[0] != 'r' || plus;
    if (mode[0] == 'w' || (long)__ussr_typeof(d) != (long)__ussr_typeof(__ussr_vec())) {
        f->data = __ussr_vec();
        f->len = 0;
        if (mode[0] != 'r')
            f->dirty = 1;
    } else {
        f->data = d;
        f->len = (long)__ussr_len(d);
    }
    if (mode[0] == 'a')
        f->pos = f->len;
    f->next = __files;
    __files = f;
    __fwritebytes(f);
    return f;
}

FILE *freopen(const char *path, const char *mode, FILE *f)
{
    if (f)
        fclose(f);
    return fopen(path, mode);
}

int fclose(FILE *f)
{
    FILE **pp;
    if (f->fd != 3) {
        fflush(f);
        return 0;
    }
    __fwritebytes(f);
    for (pp = &__files; *pp; pp = &(*pp)->next) {
        if (*pp == f) {
            *pp = f->next;
            break;
        }
    }
    free(f->path);
    free(f);
    return 0;
}

int fseek(FILE *f, long off, int whence)
{
    long p;
    if (f->fd != 3)
        return -1;
    if (whence == SEEK_SET)
        p = off;
    else if (whence == SEEK_CUR)
        p = f->pos + off;
    else
        p = f->len + off;
    if (p < 0)
        return -1;
    while (f->len < p) {
        __ussr_push(f->data, 0L);
        f->len++;
        f->dirty = 1;
    }
    f->pos = p;
    f->eof = 0;
    f->unget = -1;
    return 0;
}

long ftell(FILE *f) { return f->fd == 3 ? f->pos : -1; }
void rewind(FILE *f) { fseek(f, 0, SEEK_SET); f->err = 0; }
int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }
void clearerr(FILE *f) { f->eof = 0; f->err = 0; }
int fileno(FILE *f) { return f->fd; }
int setvbuf(FILE *f, char *buf, int mode, size_t size) { return 0; }
void setbuf(FILE *f, char *buf) { }

FILE *tmpfile(void)
{
    static int n;
    char name[32];
    sprintf(name, "usa_tmp%d.tmp", ++n);
    return fopen(name, "w+");
}

int remove(const char *path)
{
    return (long)__ussr_remove_file(__ustr(path)) == 0 ? 0 : -1;
}

int rename(const char *from, const char *to)
{
    __ussr_remove_file(__ustr(to));
    return (long)__ussr_rename_path(__ustr(from), __ustr(to)) == 0 ? 0 : -1;
}

void perror(const char *s)
{
    if (s && *s) {
        fputs(s, stderr);
        fputs(": ", stderr);
    }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

/* ---------------- formatted output ---------------- */

/* where formatted text goes: a FILE, or a buffer of cap bytes */
struct __sink {
    FILE *f;
    char *buf;
    long cap;
    long len;
};

static void __put(struct __sink *k, int c)
{
    if (k->f)
        fputc(c, k->f);
    else if (k->len < k->cap - 1)
        k->buf[k->len] = c;
    k->len++;
}

static void __puts(struct __sink *k, const char *s, long n)
{
    long i;
    for (i = 0; i < n; i++)
        __put(k, s[i]);
}

static void __pad(struct __sink *k, int c, long n)
{
    while (n-- > 0)
        __put(k, c);
}

/* digits of v in base (unsigned), backwards into p; their count */
static int __utoa(unsigned long v, int base, int upper, char *p)
{
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0;
    if (v == 0)
        p[n++] = '0';
    while (v) {
        p[n++] = dig[v % base];
        v /= base;
    }
    return n;
}

/* the number in text form: sign, prefix, digits, padded per flags */
static void __emitnum(struct __sink *k, const char *sign, const char *prefix,
                      const char *digits, long nd, long width, int left, int zero)
{
    long ls = strlen(sign), lp = strlen(prefix);
    long total = ls + lp + nd;
    if (!left && !zero)
        __pad(k, ' ', width - total);
    __puts(k, sign, ls);
    __puts(k, prefix, lp);
    if (!left && zero)
        __pad(k, '0', width - total);
    __puts(k, digits, nd);
    if (left)
        __pad(k, ' ', width - total);
}

static double __pow10(int e)
{
    double r = 1, b = 10;
    int neg = e < 0;
    if (neg)
        e = -e;
    while (e) {
        if (e & 1)
            r *= b;
        b *= b;
        e >>= 1;
    }
    return neg ? 1 / r : r;
}

/* Times mul (small enough that a limb times it stays below 2^53, where
   division is quick) the number in big[0..nb), base 10^9, least
   significant limb first; the new limb count. */
static int __bigmul(long *big, int nb, long mul)
{
    long carry = 0, v;
    int i;
    for (i = 0; i < nb; i++) {
        v = big[i] * mul + carry;
        big[i] = v % 1000000000;
        carry = v / 1000000000;
    }
    while (carry) {
        big[nb++] = carry % 1000000000;
        carry /= 1000000000;
    }
    return nb;
}

/* The exact decimal digits of a finite x > 0, without leading or
   trailing zeros, into d; their count. x = 0.d1d2d3... * 10^*dp. */
static int __exact(double x, char *d, int *dp)
{
    long big[130], m;
    int nb, e = 0, i, j, n = 0, k = 0;
    char tmp[9];
    /* x = m * 2^e, m a whole number below 2^53 */
    while (x >= 9007199254740992.0 * 4294967296.0) {
        x /= 4294967296.0;
        e += 32;
    }
    while (x >= 9007199254740992.0) {
        x /= 2;
        e++;
    }
    while (x < 1048576.0) {
        x *= 4294967296.0;
        e -= 32;
    }
    while (x < 4503599627370496.0) {
        x *= 2;
        e--;
    }
    m = (long)x;
    big[0] = m % 1000000000;
    big[1] = m / 1000000000;
    nb = big[1] ? 2 : 1;
    while (e > 0) {
        int s = e > 22 ? 22 : e;
        nb = __bigmul(big, nb, 1L << s);
        e -= s;
    }
    /* m / 2^n = m * 5^n / 10^n */
    while (e < 0) {
        int s = -e > 9 ? 9 : -e;
        long mul = 1;
        for (i = 0; i < s; i++)
            mul *= 5;
        nb = __bigmul(big, nb, mul);
        e += s;
        k += s;
    }
    for (i = nb - 1; i >= 0; i--) {
        long v = big[i];
        for (j = 8; j >= 0; j--) {
            tmp[j] = '0' + v % 10;
            v /= 10;
        }
        for (j = 0; j < 9; j++) {
            if (n == 0 && tmp[j] == '0')
                continue;
            d[n++] = tmp[j];
        }
    }
    *dp = n - k;
    while (n > 1 && d[n - 1] == '0')
        n--;
    return n;
}

/* d[0..n) rounded to keep digits, halves to even (keep may be 0 or
   less); the new count, *dp moving when 9s carry over. */
static int __rounddig(char *d, int n, int keep, int *dp)
{
    int up, i;
    if (keep >= n)
        return n;
    if (keep < 0)
        return 0;
    up = d[keep] > '5';
    if (d[keep] == '5') {
        up = keep + 1 < n || (keep > 0 && (d[keep - 1] - '0') % 2 == 1);
    }
    n = keep;
    if (!up)
        return n;
    for (i = keep - 1; i >= 0; i--) {
        if (d[i] != '9') {
            d[i]++;
            return n;
        }
        d[i] = '0';
    }
    /* 99.9 -> 100 */
    d[0] = '1';
    if (n == 0)
        n = 1;
    (*dp)++;
    return n;
}

/* The digit at place i of 0.d... (zero outside d). */
static int __dig(const char *d, int n, int i)
{
    return i >= 0 && i < n ? d[i] : '0';
}

/* x >= 0 (finite) in conversion c (f, e or g, any case) with precision
   prec into out; the length. */
static int __fmtdbl(char *out, double x, int prec, int c, int alt)
{
    char d[1100];
    int n = 0, dp = 1, len = 0, i, P, X, upper = c == 'E' || c == 'G';
    if (x > 0)
        n = __exact(x, d, &dp);
    else
        dp = 1;
    if (c == 'g' || c == 'G') {
        P = prec == 0 ? 1 : prec;
        if (n > 0)
            n = __rounddig(d, n, P, &dp);
        while (n > 1 && d[n - 1] == '0')
            n--;
        X = n > 0 ? dp - 1 : 0;
        if (P > X && X >= -4) {
            c = 'f';
            prec = P - 1 - X;
        } else {
            c = 'e';
            prec = P - 1;
        }
        if (!alt) {
            /* no trailing zeros in the fraction */
            int sig = n > 0 ? n : 1;
            if (c == 'f') {
                int frac = sig - dp;
                if (frac < prec)
                    prec = frac < 0 ? 0 : frac;
            } else if (sig - 1 < prec) {
                prec = sig - 1;
            }
        }
    }
    if (c == 'f' || c == 'F') {
        if (n > 0)
            n = __rounddig(d, n, dp + prec, &dp);
        if (dp <= 0)
            out[len++] = '0';
        for (i = 0; i < dp; i++)
            out[len++] = __dig(d, n, i);
        if (prec > 0 || alt)
            out[len++] = '.';
        for (i = 0; i < prec; i++)
            out[len++] = __dig(d, n, dp + i);
        return len;
    }
    if (n > 0)
        n = __rounddig(d, n, prec + 1, &dp);
    X = n > 0 ? dp - 1 : 0;
    out[len++] = __dig(d, n, 0);
    if (prec > 0 || alt)
        out[len++] = '.';
    for (i = 1; i <= prec; i++)
        out[len++] = __dig(d, n, i);
    out[len++] = upper ? 'E' : 'e';
    out[len++] = X < 0 ? '-' : '+';
    if (X < 0)
        X = -X;
    if (X >= 100)
        out[len++] = '0' + X / 100;
    out[len++] = '0' + X / 10 % 10;
    out[len++] = '0' + X % 10;
    return len;
}

static int __vformat(struct __sink *k, const char *fmt, va_list ap)
{
    char num[512];
    const char *p;
    for (p = fmt; *p; p++) {
        int left = 0, plus = 0, space = 0, alt = 0, zero = 0;
        long width = 0;
        int prec = -1, lng = 0, c;
        if (*p != '%') {
            __put(k, *p);
            continue;
        }
        p++;
        for (;; p++) {
            if (*p == '-') left = 1;
            else if (*p == '+') plus = 1;
            else if (*p == ' ') space = 1;
            else if (*p == '#') alt = 1;
            else if (*p == '0') zero = 1;
            else break;
        }
        if (*p == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            p++;
        } else {
            while (isdigit(*p))
                width = width * 10 + (*p++ - '0');
        }
        if (*p == '.') {
            p++;
            prec = 0;
            if (*p == '*') {
                prec = va_arg(ap, int);
                p++;
            } else {
                while (isdigit(*p))
                    prec = prec * 10 + (*p++ - '0');
            }
        }
        for (;; p++) {
            if (*p == 'l' || *p == 'q' || *p == 'j' || *p == 'z' || *p == 't') lng = 1;
            else if (*p == 'h' || *p == 'L') ;
            else if (*p == 'I' && p[1] == '6' && p[2] == '4') {
                /* MSVC's %I64d */
                lng = 1;
                p += 2;
            } else if (*p == 'I' && p[1] == '3' && p[2] == '2') p += 2;
            else if (*p == 'I') lng = 1;
            else break;
        }
        c = *p;
        if (c == 0)
            break;
        if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'o' || c == 'p' || c == 'b') {
            unsigned long u;
            const char *sign = "", *prefix = "";
            int base = 10, nd, i;
            char digits[80];
            if (c == 'p') {
                u = (unsigned long)va_arg(ap, void *);
                base = 16;
                prefix = "0x";
            } else if (c == 'd' || c == 'i') {
                long v = lng ? va_arg(ap, long) : va_arg(ap, int);
                if (v < 0) {
                    sign = "-";
                    u = -(unsigned long)v;
                } else {
                    u = v;
                    sign = plus ? "+" : space ? " " : "";
                }
            } else {
                u = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned int);
                if (c == 'x' || c == 'X') {
                    base = 16;
                    if (alt && u)
                        prefix = c == 'x' ? "0x" : "0X";
                } else if (c == 'o') {
                    base = 8;
                    if (alt)
                        prefix = "0";
                } else if (c == 'b') {
                    base = 2;
                }
            }
            nd = __utoa(u, base, c == 'X', num);
            if (prec == 0 && u == 0)
                nd = 0;
            i = 0;
            while (nd + i < prec)
                digits[i++] = '0';
            while (nd > 0)
                digits[i++] = num[--nd];
            if (prec >= 0)
                zero = 0;
            __emitnum(k, sign, prefix, digits, i, width, left, zero);
        } else if (c == 'c') {
            char ch = va_arg(ap, int);
            if (!left) __pad(k, ' ', width - 1);
            __put(k, ch);
            if (left) __pad(k, ' ', width - 1);
        } else if (c == 's') {
            const char *s = va_arg(ap, const char *);
            long n;
            if (!s)
                s = "(null)";
            n = prec >= 0 ? (long)strnlen(s, prec) : (long)strlen(s);
            if (!left) __pad(k, ' ', width - n);
            __puts(k, s, n);
            if (left) __pad(k, ' ', width - n);
        } else if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G') {
            double x = va_arg(ap, double);
            const char *sign = "";
            int n;
            if (x < 0) {
                sign = "-";
                x = -x;
            } else if (plus) sign = "+";
            else if (space) sign = " ";
            if (prec < 0)
                prec = 6;
            if (x != x) {
                strcpy(num, "nan");
                n = 3;
                zero = 0;
            } else if (x > 1.7976931348623157e308) {
                strcpy(num, "inf");
                n = 3;
                zero = 0;
            } else {
                if (prec > 400)
                    prec = 400;
                n = __fmtdbl(num, x, prec, c, alt);
            }
            __emitnum(k, sign, "", num, n, width, left, zero);
        } else if (c == 'n') {
            int *ip = va_arg(ap, int *);
            *ip = k->len;
        } else {
            __put(k, c);
        }
    }
    return k->len;
}

int vfprintf(FILE *f, const char *fmt, va_list ap)
{
    struct __sink k;
    int n;
    k.f = f;
    k.buf = 0;
    k.cap = 0;
    k.len = 0;
    n = __vformat(&k, fmt, ap);
    if (f == stderr)
        fflush(stderr);
    return n;
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    struct __sink k;
    int r;
    k.f = 0;
    k.buf = s;
    k.cap = n;
    k.len = 0;
    r = __vformat(&k, fmt, ap);
    if (n > 0)
        s[k.len < (long)n - 1 ? k.len : (long)n - 1] = 0;
    return r;
}

int vsprintf(char *s, const char *fmt, va_list ap)
{
    return vsnprintf(s, 1L << 40, fmt, ap);
}

int vprintf(const char *fmt, va_list ap)
{
    return vfprintf(stdout, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfprintf(f, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *s, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(s, 1L << 40, fmt, ap);
    va_end(ap);
    return n;
}

int snprintf(char *s, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(s, size, fmt, ap);
    va_end(ap);
    return n;
}

/* ---------------- number parsing ---------------- */

unsigned long strtoul(const char *s, char **end, int base)
{
    const char *p = s;
    unsigned long v = 0;
    int neg = 0, any = 0, d;
    while (isspace(*p))
        p++;
    if (*p == '-' || *p == '+')
        neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && isxdigit(p[2])) {
        p += 2;
        base = 16;
    } else if (base == 0 && p[0] == '0') {
        base = 8;
    } else if (base == 0) {
        base = 10;
    }
    for (;; p++) {
        if (isdigit(*p)) d = *p - '0';
        else if (*p >= 'a' && *p <= 'z') d = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') d = *p - 'A' + 10;
        else break;
        if (d >= base)
            break;
        v = v * base + d;
        any = 1;
    }
    if (end)
        *end = (char *)(any ? p : s);
    return neg ? -v : v;
}

long strtol(const char *s, char **end, int base)
{
    return (long)strtoul(s, end, base);
}

long long strtoll(const char *s, char **end, int base) { return strtol(s, end, base); }
unsigned long long strtoull(const char *s, char **end, int base) { return strtoul(s, end, base); }
int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }
long long atoll(const char *s) { return strtol(s, NULL, 10); }

double strtod(const char *s, char **end)
{
    const char *p = s;
    long m = 0;
    int ex = 0, neg = 0, nd = 0, any = 0;
    double v;
    while (isspace(*p))
        p++;
    if (*p == '-' || *p == '+')
        neg = *p++ == '-';
    if (strncasecmp(p, "inf", 3) == 0) {
        p += 3;
        if (strncasecmp(p, "inity", 5) == 0)
            p += 5;
        if (end)
            *end = (char *)p;
        return neg ? -HUGE_VAL : HUGE_VAL;
    }
    if (strncasecmp(p, "nan", 3) == 0) {
        if (end)
            *end = (char *)(p + 3);
        return __nan();
    }
    while (isdigit(*p)) {
        if (nd < 18) {
            m = m * 10 + (*p - '0');
            if (m)
                nd++;
        } else {
            ex++;
        }
        p++;
        any = 1;
    }
    if (*p == '.') {
        p++;
        while (isdigit(*p)) {
            if (nd < 18) {
                m = m * 10 + (*p - '0');
                if (m)
                    nd++;
                ex--;
            }
            p++;
            any = 1;
        }
    }
    if (any && (*p == 'e' || *p == 'E')) {
        const char *q = p + 1;
        int eneg = 0, e = 0;
        if (*q == '-' || *q == '+')
            eneg = *q++ == '-';
        if (isdigit(*q)) {
            while (isdigit(*q))
                e = e * 10 + (*q++ - '0');
            ex += eneg ? -e : e;
            p = q;
        }
    }
    if (end)
        *end = (char *)(any ? p : s);
    v = (double)m;
    if (ex < 0) {
        while (ex < -22) {
            v /= 1e22;
            ex += 22;
        }
        v /= __pow10(-ex);
    } else if (ex > 0) {
        v *= __pow10(ex);
    }
    return neg ? -v : v;
}

float strtof(const char *s, char **end) { return strtod(s, end); }
double atof(const char *s) { return strtod(s, NULL); }

/* ---------------- formatted input ---------------- */

/* where scanned text comes from: a FILE or a string */
struct __src {
    FILE *f;
    const char *s;
    long n;    /* characters taken */
};

static int __getc(struct __src *r)
{
    int c;
    if (r->f)
        c = fgetc(r->f);
    else {
        c = (unsigned char)*r->s;
        if (c == 0)
            c = EOF;
        else
            r->s++;
    }
    if (c != EOF)
        r->n++;
    return c;
}

static void __ungetc(struct __src *r, int c)
{
    if (c == EOF)
        return;
    r->n--;
    if (r->f)
        ungetc(c, r->f);
    else
        r->s--;
}

static int __vscan(struct __src *r, const char *fmt, va_list ap)
{
    int count = 0, c;
    const char *p;
    for (p = fmt; *p; p++) {
        int skip = 0, lng = 0, half = 0;
        long width = 0;
        if (isspace(*p)) {
            do
                c = __getc(r);
            while (isspace(c));
            __ungetc(r, c);
            continue;
        }
        if (*p != '%' || p[1] == '%') {
            if (*p == '%')
                p++;
            c = __getc(r);
            if (c != *p) {
                __ungetc(r, c);
                return count;
            }
            continue;
        }
        p++;
        if (*p == '*') {
            skip = 1;
            p++;
        }
        while (isdigit(*p))
            width = width * 10 + (*p++ - '0');
        for (;; p++) {
            if (*p == 'l' || *p == 'L' || *p == 'q' || *p == 'j' || *p == 'z' || *p == 't') lng++;
            else if (*p == 'h') half++;
            else break;
        }
        if (width == 0)
            width = 1L << 40;
        if (*p == 'n') {
            if (!skip)
                *va_arg(ap, int *) = r->n;
            continue;
        }
        if (*p != 'c' && *p != '[') {
            do
                c = __getc(r);
            while (isspace(c));
            __ungetc(r, c);
        }
        if (*p == 'd' || *p == 'i' || *p == 'u' || *p == 'x' || *p == 'X' || *p == 'o') {
            char buf[80];
            int n = 0, base = *p == 'x' || *p == 'X' ? 16 : *p == 'o' ? 8 : *p == 'i' ? 0 : 10;
            long v;
            c = __getc(r);
            if ((c == '-' || c == '+') && n < width) {
                buf[n++] = c;
                c = __getc(r);
            }
            while (c != EOF && n < width && n < 78 && (isxdigit(c) || c == 'x' || c == 'X')) {
                if (!(base == 16 || base == 0) && !isdigit(c))
                    break;
                if ((c == 'x' || c == 'X') && !(n > 0 && buf[n - 1] == '0'))
                    break;
                buf[n++] = c;
                c = __getc(r);
            }
            __ungetc(r, c);
            buf[n] = 0;
            if (n == 0 || (n == 1 && (buf[0] == '-' || buf[0] == '+')))
                return count ? count : (c == EOF ? EOF : count);
            v = strtol(buf, NULL, base);
            if (!skip) {
                if (lng) *va_arg(ap, long *) = v;
                else if (half > 1) *va_arg(ap, char *) = v;
                else if (half) *va_arg(ap, short *) = v;
                else *va_arg(ap, int *) = v;
                count++;
            }
        } else if (*p == 'f' || *p == 'e' || *p == 'g' || *p == 'E' || *p == 'G' || *p == 'a') {
            char buf[80];
            int n = 0;
            double v;
            c = __getc(r);
            while (c != EOF && n < width && n < 78 && (isdigit(c) || c == '.' || c == '-' || c == '+' || c == 'e' || c == 'E')) {
                buf[n++] = c;
                c = __getc(r);
            }
            __ungetc(r, c);
            buf[n] = 0;
            if (n == 0)
                return count ? count : (c == EOF ? EOF : count);
            v = strtod(buf, NULL);
            if (!skip) {
                if (lng) *va_arg(ap, double *) = v;
                else *va_arg(ap, float *) = v;
                count++;
            }
        } else if (*p == 's') {
            char *d = skip ? 0 : va_arg(ap, char *);
            int n = 0;
            c = __getc(r);
            if (c == EOF)
                return count ? count : EOF;
            while (c != EOF && !isspace(c) && n < width) {
                if (d)
                    d[n] = c;
                n++;
                c = __getc(r);
            }
            __ungetc(r, c);
            if (d) {
                d[n] = 0;
                count++;
            }
        } else if (*p == 'c') {
            char *d = skip ? 0 : va_arg(ap, char *);
            long n = 0;
            if (width == 1L << 40)
                width = 1;
            while (n < width) {
                c = __getc(r);
                if (c == EOF)
                    return count ? count : EOF;
                if (d)
                    d[n] = c;
                n++;
            }
            if (d)
                count++;
        } else if (*p == '[') {
            char set[256];
            int neg = 0, i, n = 0;
            char *d;
            for (i = 0; i < 256; i++)
                set[i] = 0;
            p++;
            if (*p == '^') {
                neg = 1;
                p++;
            }
            if (*p == ']')
                set[(unsigned char)*p++] = 1;
            while (*p && *p != ']') {
                if (p[1] == '-' && p[2] && p[2] != ']') {
                    for (i = (unsigned char)p[0]; i <= (unsigned char)p[2]; i++)
                        set[i] = 1;
                    p += 3;
                } else {
                    set[(unsigned char)*p++] = 1;
                }
            }
            d = skip ? 0 : va_arg(ap, char *);
            c = __getc(r);
            while (c != EOF && n < width && set[c] != neg) {
                if (d)
                    d[n] = c;
                n++;
                c = __getc(r);
            }
            __ungetc(r, c);
            if (n == 0)
                return count;
            if (d) {
                d[n] = 0;
                count++;
            }
        } else {
            return count;
        }
    }
    return count;
}

int vsscanf(const char *s, const char *fmt, va_list ap)
{
    struct __src r;
    r.f = 0;
    r.s = s;
    r.n = 0;
    return __vscan(&r, fmt, ap);
}

int vfscanf(FILE *f, const char *fmt, va_list ap)
{
    struct __src r;
    r.f = f;
    r.s = 0;
    r.n = 0;
    return __vscan(&r, fmt, ap);
}

int sscanf(const char *s, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsscanf(s, fmt, ap);
    va_end(ap);
    return n;
}

int fscanf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfscanf(f, fmt, ap);
    va_end(ap);
    return n;
}

int scanf(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfscanf(stdin, fmt, ap);
    va_end(ap);
    return n;
}

/* ---------------- stdlib ---------------- */

static unsigned long __rand_next = 1;

int rand(void)
{
    __rand_next = __rand_next * 1103515245 + 12345;
    return (unsigned int)(__rand_next / 65536) % 32768;
}

void srand(unsigned int seed) { __rand_next = seed; }
int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

div_t div(int a, int b)
{
    div_t r;
    r.quot = a / b;
    r.rem = a % b;
    return r;
}

ldiv_t ldiv(long a, long b)
{
    ldiv_t r;
    r.quot = a / b;
    r.rem = a % b;
    return r;
}

static void __swap(char *a, char *b, size_t n)
{
    while (n--) {
        char t = *a;
        *a++ = *b;
        *b++ = t;
    }
}

/* quicksort with insertion sort for small parts */
static void __qsort(char *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    while (n > 8) {
        size_t i, last = 0;
        __swap(base, base + (n / 2) * size, size);
        for (i = 1; i < n; i++) {
            if (cmp(base + i * size, base) < 0) {
                last++;
                __swap(base + last * size, base + i * size, size);
            }
        }
        __swap(base, base + last * size, size);
        /* recurse into the smaller side */
        if (last < n - last - 1) {
            __qsort(base, last, size, cmp);
            base += (last + 1) * size;
            n -= last + 1;
        } else {
            __qsort(base + (last + 1) * size, n - last - 1, size, cmp);
            n = last;
        }
    }
    {
        size_t i, j;
        for (i = 1; i < n; i++)
            for (j = i; j > 0 && cmp(base + (j - 1) * size, base + j * size) > 0; j--)
                __swap(base + (j - 1) * size, base + j * size, size);
    }
}

void qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    __qsort(base, n, size, cmp);
}

void *bsearch(const void *key, const void *base, size_t n, size_t size, int (*cmp)(const void *, const void *))
{
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        const char *e = (const char *)base + mid * size;
        int c = cmp(key, e);
        if (c == 0)
            return (void *)e;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}

char *getenv(const char *name)
{
    __uval v = __ussr_getenv(__ustr(name));
    if ((long)__ussr_typeof(v) != (long)__ussr_typeof(__U("")))
        return NULL;
    return __cstr(v);
}

int system(const char *cmd) { return -1; }

int atexit(void (*f)(void))
{
    if (__natexit == 32)
        return -1;
    __atexit_fns[__natexit++] = f;
    return 0;
}

void exit(int status)
{
    while (__natexit > 0)
        __atexit_fns[--__natexit]();
    fflush(NULL);
    __exit(status & 255);
}

void _Exit(int status)
{
    fflush(NULL);
    __exit(status & 255);
}

void abort(void)
{
    fflush(NULL);
    __exit(134);
}

void __assert_fail(const char *expr, const char *file, int line, const char *func)
{
    fprintf(stderr, "%s:%d: %s: Assertion `%s' failed.\n", file, line, func, expr);
    abort();
}

__sighandler_t signal(int sig, __sighandler_t h) { return SIG_DFL; }

int raise(int sig)
{
    if (sig == SIGABRT)
        abort();
    exit(128 + sig);
    return 0;
}

void longjmp(jmp_buf b, int v)
{
    fputs("usa: longjmp is not supported\n", stderr);
    abort();
}

char *setlocale(int cat, const char *loc) { return "C"; }

struct lconv *localeconv(void)
{
    static struct lconv l;
    l.decimal_point = ".";
    l.thousands_sep = "";
    return &l;
}

int isatty(int fd) { return fd <= 2; }
int unlink(const char *path) { return remove(path); }
/* (file_exists needs call-site records .ru files leave out) */
int access(const char *path, int mode)
{
    __uval p = __ustr(path);
    long h;
    if ((long)__ussr_is_dir(p) == 1)
        return 0;
    h = (long)__ussr_open_file(p, __U("rb"));
    if (h < 0)
        return -1;
    __ussr_close_file(h);
    return 0;
}

/* ---------------- time ---------------- */

time_t time(time_t *t)
{
    time_t now = __time();
    if (t)
        *t = now;
    return now;
}

clock_t clock(void) { return __time() * CLOCKS_PER_SEC; }
double difftime(time_t a, time_t b) { return (double)(a - b); }

struct tm *gmtime(const time_t *t)
{
    static struct tm tm;
    long s = *t, days, y = 1970;
    int mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int m = 0;
    days = s / 86400;
    s %= 86400;
    if (s < 0) {
        s += 86400;
        days--;
    }
    tm.tm_hour = s / 3600;
    tm.tm_min = s / 60 % 60;
    tm.tm_sec = s % 60;
    tm.tm_wday = (days + 4) % 7;
    for (;;) {
        int len = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 366 : 365;
        if (days < len)
            break;
        days -= len;
        y++;
    }
    tm.tm_year = y - 1900;
    tm.tm_yday = days;
    if (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))
        mdays[1] = 29;
    while (days >= mdays[m]) {
        days -= mdays[m];
        m++;
    }
    tm.tm_mon = m;
    tm.tm_mday = days + 1;
    tm.tm_isdst = 0;
    return &tm;
}

struct tm *localtime(const time_t *t) { return gmtime(t); }

/* ---------------- math ---------------- */

double __nan(void)
{
    double inf = 1e300 * 1e300;
    return inf - inf;
}

int isnan(double x) { return x != x; }
int isinf(double x) { return x == x && (x > 1.7976931348623157e308 || x < -1.7976931348623157e308); }
int isfinite(double x) { return !isnan(x) && !isinf(x); }
double fabs(double x) { return x < 0 ? -x : x; }

double floor(double x)
{
    double t;
    if (x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;
    t = (double)(long)x;
    return t > x ? t - 1 : t;
}

double ceil(double x)
{
    double t;
    if (x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;
    t = (double)(long)x;
    return t < x ? t + 1 : t;
}

double trunc(double x)
{
    if (x != x || x >= 4503599627370496.0 || x <= -4503599627370496.0)
        return x;
    return (double)(long)x;
}

double round(double x) { return x < 0 ? -floor(-x + 0.5) : floor(x + 0.5); }

/* to nearest, ties to even */
double rint(double x)
{
    double f = floor(x), d = x - f;
    if (d > 0.5 || (d == 0.5 && fmod(f, 2) != 0))
        f += 1;
    return f;
}

double nearbyint(double x) { return rint(x); }
long lrint(double x) { return (long)rint(x); }
long lround(double x) { return (long)round(x); }
double fmin(double a, double b) { return a < b ? a : b; }
double fmax(double a, double b) { return a > b ? a : b; }

double fmod(double x, double y)
{
    return x - trunc(x / y) * y;
}

double modf(double x, double *ip)
{
    *ip = trunc(x);
    return x - *ip;
}

double sqrt(double x)
{
    double r, last;
    int i;
    if (x < 0)
        return __nan();
    if (x == 0 || x != x || x > 1.7976931348623157e308)
        return x;
    r = x > 1 ? x / 2 : 1;
    /* scale near 1 for quick convergence */
    {
        double s = 1;
        while (r > 1e10) {
            r /= 1e10;
            s *= 1e5;
        }
        r = r * s;
    }
    for (i = 0; i < 200; i++) {
        last = r;
        r = (r + x / r) / 2;
        if (r == last)
            break;
    }
    return r;
}

double log(double x)
{
    if (x < 0)
        return __nan();
    if (x == 0)
        return -HUGE_VAL;
    return (double)__ussr_ln(x);
}

double log10(double x) { return log(x) / M_LN10; }
double log2(double x) { return log(x) / M_LN2; }

double exp(double x)
{
    double r = 1, t = 1;
    int k, i;
    if (x != x)
        return x;
    if (x > 709.8)
        return HUGE_VAL;
    if (x < -745)
        return 0;
    /* x = k ln2 + f, |f| <= ln2/2 */
    k = (int)floor(x / M_LN2 + 0.5);
    x = x - k * M_LN2;
    for (i = 1; i < 30; i++) {
        t = t * x / i;
        r += t;
    }
    return ldexp(r, k);
}

double ldexp(double x, int e)
{
    while (e > 0) {
        int s = e > 60 ? 60 : e;
        x *= (double)(1L << s);
        e -= s;
    }
    while (e < 0) {
        int s = e < -60 ? 60 : -e;
        x /= (double)(1L << s);
        e += s;
    }
    return x;
}

double frexp(double x, int *e)
{
    int n = 0;
    if (x == 0 || x != x) {
        *e = 0;
        return x;
    }
    while (fabs(x) >= 1) {
        x /= 2;
        n++;
    }
    while (fabs(x) < 0.5) {
        x *= 2;
        n--;
    }
    *e = n;
    return x;
}

double pow(double x, double y)
{
    long n;
    double r;
    if (y == 0)
        return 1;
    if (y == (double)(long)y && fabs(y) < 1e18) {
        /* whole powers by squaring */
        int neg;
        n = (long)y;
        neg = n < 0;
        if (neg)
            n = -n;
        r = 1;
        while (n) {
            if (n & 1)
                r *= x;
            x *= x;
            n >>= 1;
        }
        return neg ? 1 / r : r;
    }
    if (x < 0)
        return __nan();
    if (x == 0)
        return 0;
    return exp(y * log(x));
}

double sin(double x) { return (double)__ussr_sin(x); }
/* cos(x) = sin(x + pi/2), with the rounding of x + pi/2 (and pi/2's own
   tail) put back by sin(s + e) ~ sin(s) + e cos(s) */
double cos(double x)
{
    double p1 = 1.5707963267948966, p2 = 6.123233995736766e-17;
    double s = x + p1, bv = s - x, av = s - bv;
    double e = ((x - av) + (p1 - bv)) + p2;
    return (double)__ussr_sin(s) + e * (double)__ussr_sin(s + p1);
}
double tan(double x) { return sin(x) / cos(x); }

double atan(double x)
{
    /* reduce to |x| <= 0.4 with atan(x) = 2 atan(x / (1 + sqrt(1 + x^2))) */
    double r = 0, t, x2;
    int i, twice = 0, neg = x < 0;
    if (neg)
        x = -x;
    if (x > 1e300)
        return neg ? -M_PI / 2 : M_PI / 2;
    while (x > 0.4) {
        x = x / (1 + sqrt(1 + x * x));
        twice++;
    }
    x2 = x * x;
    t = x;
    for (i = 1; i < 60; i += 2) {
        r += ((i >> 1) & 1 ? -t : t) / i;
        t *= x2;
    }
    while (twice--)
        r *= 2;
    return neg ? -r : r;
}

double atan2(double y, double x)
{
    if (x > 0)
        return atan(y / x);
    if (x < 0)
        return y >= 0 ? atan(y / x) + M_PI : atan(y / x) - M_PI;
    if (y > 0)
        return M_PI / 2;
    if (y < 0)
        return -M_PI / 2;
    return 0;
}

double asin(double x)
{
    if (x > 1 || x < -1)
        return __nan();
    if (x == 1)
        return M_PI / 2;
    if (x == -1)
        return -M_PI / 2;
    return atan(x / sqrt(1 - x * x));
}

double acos(double x) { return M_PI / 2 - asin(x); }
double sinh(double x) { return (exp(x) - exp(-x)) / 2; }
double cosh(double x) { return (exp(x) + exp(-x)) / 2; }

double tanh(double x)
{
    double a, b;
    if (x > 20)
        return 1;
    if (x < -20)
        return -1;
    a = exp(x);
    b = exp(-x);
    return (a - b) / (a + b);
}

double hypot(double x, double y) { return sqrt(x * x + y * y); }

double cbrt(double x)
{
    double r;
    int i, neg = x < 0;
    if (neg)
        x = -x;
    if (x == 0)
        return 0;
    r = exp(log(x) / 3);
    for (i = 0; i < 4; i++)
        r = r - (r * r * r - x) / (3 * r * r);
    return neg ? -r : r;
}

/* ---------------- start ---------------- */

static int __start2(long argc, __uval argv, long memlen)
{
    char **av;
    long i;
    __memlen = memlen;
    __brk = __data_end() + __STACK;
    __stdin_file.fd = 0;
    __stdin_file.rd = 1;
    __stdin_file.unget = -1;
    __stdout_file.fd = 1;
    __stdout_file.wr = 1;
    __stdout_file.unget = -1;
    __stderr_file.fd = 2;
    __stderr_file.wr = 1;
    __stderr_file.unget = -1;
    __ginit();
    av = malloc((argc + 2) * sizeof(char *));
    av[0] = "a.ru";
    for (i = 0; i < argc; i++)
        av[i + 1] = __cstr(__ussr_at(argv, i));
    av[argc + 1] = 0;
    exit(main(argc + 1, av));
    return 0;
}

int __start(long argc, __uval argv)
{
    long need = __data_end() + __STACK + 262144, n = 1, i;
    __uval s = __U(" "), v, idx;
    /* C memory: a string of spaces doubled up, as bytes, cleared */
    while (n < need) {
        s = __ussr_concat(s, s);
        n = n + n;
    }
    v = __ussr_text_bytes(s);
    __ussr_clear_bytes(v);
    __setmem(v);
    __setsp(__data_end());
    /* __f2i reads integers 0..65535 from here */
    idx = __ussr_vec();
    for (i = 0; i < 65536; i++)
        __ussr_push(idx, i);
    __setidx(idx);
    return __start2(argc, argv, n);
}
