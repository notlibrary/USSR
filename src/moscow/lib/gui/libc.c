/* libc.c - the C library of /lib/gui.so: what petersburg.c and ttf.h
 * take from a hosted C library, on Moscow. The heap is an arena the
 * kernel gives gui_start; stderr goes to the first serial port (and is
 * kept: gui_message); fopen reads files the kernel has handed over with
 * gui_file_put, since the file system is the kernel's (Belarus, in
 * USSR). */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

int errno;

static inline void port_out8(uint16_t port, uint8_t v)
{
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint8_t port_in8(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void port_out16(uint16_t port, uint16_t v)
{
    __asm__ volatile ("outw %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint16_t port_in16(uint16_t port)
{
    uint16_t v;
    __asm__ volatile ("inw %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static inline void port_out32(uint16_t port, uint32_t v)
{
    __asm__ volatile ("outl %0, %1" : : "a"(v), "Nd"(port));
}
static inline uint32_t port_in32(uint16_t port)
{
    uint32_t v;
    __asm__ volatile ("inl %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}

/* --- strings */

int strcmp(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b)
        ++a, ++b;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    for (; n > 0; --n, ++a, ++b)
    {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (*a == '\0')
            return 0;
    }
    return 0;
}

char *strchr(const char *s, int c)
{
    for (;; ++s)
    {
        if (*s == (char)c)
            return (char *)s;
        if (*s == '\0')
            return NULL;
    }
}

char *strrchr(const char *s, int c)
{
    const char *last = NULL;
    for (;; ++s)
    {
        if (*s == (char)c)
            last = s;
        if (*s == '\0')
            return (char *)last;
    }
}

char *strstr(const char *s, const char *t)
{
    size_t n = strlen(t);
    for (; *s != '\0'; ++s)
        if (strncmp(s, t, n) == 0)
            return (char *)s;
    return n == 0 ? (char *)s : NULL;
}

char *strcpy(char *d, const char *s)
{
    memcpy(d, s, strlen(s) + 1);
    return d;
}

char *strncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i] != '\0'; ++i)
        d[i] = s[i];
    for (; i < n; ++i)
        d[i] = '\0';
    return d;
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

long strtol(const char *s, char **end, int base)
{
    const char *p = s;
    long v = 0;
    int neg = 0;
    int any = 0;

    while (*p == ' ' || (*p >= 9 && *p <= 13))
        ++p;
    if (*p == '-' || *p == '+')
        neg = *p++ == '-';
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] | 32) == 'x')
    {
        p += 2;
        base = 16;
    }
    else if (base == 0)
        base = p[0] == '0' ? 8 : 10;
    for (;; ++p)
    {
        int d;
        if (*p >= '0' && *p <= '9')
            d = *p - '0';
        else if ((*p | 32) >= 'a' && (*p | 32) <= 'z')
            d = (*p | 32) - 'a' + 10;
        else
            break;
        if (d >= base)
            break;
        v = v * base + d;
        any = 1;
    }
    if (end != NULL)
        *end = (char *)(any ? p : s);
    return neg ? -v : v;
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }

double strtod(const char *s, char **end)
{
    const char *p = s;
    double v = 0.0;
    double scale = 1.0;
    int neg = 0;
    int any = 0;

    while (*p == ' ' || (*p >= 9 && *p <= 13))
        ++p;
    if (*p == '-' || *p == '+')
        neg = *p++ == '-';
    for (; *p >= '0' && *p <= '9'; ++p, any = 1)
        v = v * 10.0 + (*p - '0');
    if (*p == '.')
        for (++p; *p >= '0' && *p <= '9'; ++p, any = 1)
        {
            scale /= 10.0;
            v += (*p - '0') * scale;
        }
    if (any && (*p | 32) == 'e')
    {
        char *after;
        long e = strtol(p + 1, &after, 10);
        if (after != p + 1)
        {
            p = after;
            for (; e > 0; --e)
                v *= 10.0;
            for (; e < 0; ++e)
                v /= 10.0;
        }
    }
    if (end != NULL)
        *end = (char *)(any ? p : s);
    return neg ? -v : v;
}

/* --- the heap: first fit over one arena, blocks with a size header and
 * footer so that neighbours merge when freed */

typedef struct heap_block
{
    size_t size;                /* the whole block, header included; bit 0: used */
    struct heap_block *next;    /* free blocks only */
    struct heap_block *prev;
} heap_block_t;

#define HEAP_ALIGN 16
#define HEAP_HEADER 16
#define HEAP_MIN 48

static unsigned char *heap_start;
static unsigned char *heap_end;
static heap_block_t *heap_free;
static size_t heap_used;

static size_t *heap_footer(heap_block_t *b)
{
    return (size_t *)((unsigned char *)b + (b->size & ~(size_t)1) - sizeof(size_t));
}

static void heap_unlink(heap_block_t *b)
{
    if (b->prev != NULL)
        b->prev->next = b->next;
    else
        heap_free = b->next;
    if (b->next != NULL)
        b->next->prev = b->prev;
}

static void heap_link(heap_block_t *b)
{
    b->prev = NULL;
    b->next = heap_free;
    if (heap_free != NULL)
        heap_free->prev = b;
    heap_free = b;
}

static void heap_mark(heap_block_t *b, size_t size, int used)
{
    b->size = size | (size_t)(used ? 1 : 0);
    *heap_footer(b) = b->size;
}

static void heap_init(void *arena, size_t size)
{
    uintptr_t a = ((uintptr_t)arena + HEAP_ALIGN - 1) & ~(uintptr_t)(HEAP_ALIGN - 1);
    size -= a - (uintptr_t)arena;
    size &= ~(size_t)(HEAP_ALIGN - 1);
    heap_start = (unsigned char *)a;
    heap_end = heap_start + size;
    heap_free = NULL;
    heap_used = 0;
    heap_mark((heap_block_t *)heap_start, size, 0);
    heap_link((heap_block_t *)heap_start);
}

void *malloc(size_t n)
{
    size_t need = (n + HEAP_HEADER + sizeof(size_t) + HEAP_ALIGN - 1) &
                  ~(size_t)(HEAP_ALIGN - 1);
    heap_block_t *b;

    if (need < HEAP_MIN)
        need = HEAP_MIN;
    for (b = heap_free; b != NULL; b = b->next)
        if (b->size >= need)
            break;
    if (b == NULL)
    {
        fprintf(stderr, "gui: out of memory (%lu bytes asked, %lu in use)\n",
                (unsigned long)n, (unsigned long)heap_used);
        return NULL;
    }
    heap_unlink(b);
    if (b->size - need >= HEAP_MIN)
    {
        heap_block_t *rest = (heap_block_t *)((unsigned char *)b + need);
        heap_mark(rest, b->size - need, 0);
        heap_link(rest);
        heap_mark(b, need, 1);
    }
    else
        heap_mark(b, b->size, 1);
    heap_used += b->size & ~(size_t)1;
    return (unsigned char *)b + HEAP_HEADER;
}

void free(void *p)
{
    heap_block_t *b;
    size_t size;

    if (p == NULL)
        return;
    b = (heap_block_t *)((unsigned char *)p - HEAP_HEADER);
    size = b->size & ~(size_t)1;
    heap_used -= size;
    /* the next block */
    if ((unsigned char *)b + size < heap_end)
    {
        heap_block_t *next = (heap_block_t *)((unsigned char *)b + size);
        if ((next->size & 1) == 0)
        {
            heap_unlink(next);
            size += next->size;
        }
    }
    /* the previous one, by its footer */
    if ((unsigned char *)b > heap_start)
    {
        size_t prev_size = *(size_t *)((unsigned char *)b - sizeof(size_t));
        if ((prev_size & 1) == 0)
        {
            heap_block_t *prev = (heap_block_t *)((unsigned char *)b - prev_size);
            heap_unlink(prev);
            size += prev_size;
            b = prev;
        }
    }
    heap_mark(b, size, 0);
    heap_link(b);
}

void *calloc(size_t count, size_t size)
{
    void *p;
    if (size != 0 && count > (size_t)-1 / size)
        return NULL;
    p = malloc(count * size);
    if (p != NULL)
        memset(p, 0, count * size);
    return p;
}

void *realloc(void *p, size_t n)
{
    heap_block_t *b;
    size_t have;
    void *q;

    if (p == NULL)
        return malloc(n);
    if (n == 0)
    {
        free(p);
        return NULL;
    }
    b = (heap_block_t *)((unsigned char *)p - HEAP_HEADER);
    have = (b->size & ~(size_t)1) - HEAP_HEADER - sizeof(size_t);
    if (n <= have)
        return p;
    q = malloc(n);
    if (q != NULL)
    {
        memcpy(q, p, have);
        free(p);
    }
    return q;
}

/* --- formatted text */

typedef struct
{
    char *buf;
    size_t size;
    size_t len;
} fmt_out_t;

static void fmt_put(fmt_out_t *o, char c)
{
    if (o->len + 1 < o->size)
        o->buf[o->len] = c;
    ++o->len;
}

static void fmt_pad(fmt_out_t *o, const char *s, size_t n, int width, int left, char fill)
{
    int pad = width > (int)n ? width - (int)n : 0;
    if (!left)
        while (pad-- > 0)
            fmt_put(o, fill);
    while (n-- > 0)
        fmt_put(o, *s++);
    if (left)
        while (pad-- > 0)
            fmt_put(o, ' ');
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    fmt_out_t o = { buf, size, 0 };

    for (; *fmt != '\0'; ++fmt)
    {
        int left = 0;
        char fill = ' ';
        int width = 0;
        int precision = -1;
        int longs = 0;
        char tmp[64];
        size_t n = 0;
        const char *s = tmp;

        if (*fmt != '%')
        {
            fmt_put(&o, *fmt);
            continue;
        }
        ++fmt;
        for (;; ++fmt)
        {
            if (*fmt == '-')
                left = 1;
            else if (*fmt == '0')
                fill = '0';
            else if (*fmt != '+' && *fmt != ' ' && *fmt != '#')
                break;
        }
        if (*fmt == '*')
        {
            width = va_arg(ap, int);
            ++fmt;
        }
        for (; *fmt >= '0' && *fmt <= '9'; ++fmt)
            width = width * 10 + (*fmt - '0');
        if (*fmt == '.')
        {
            precision = 0;
            ++fmt;
            if (*fmt == '*')
            {
                precision = va_arg(ap, int);
                ++fmt;
            }
            for (; *fmt >= '0' && *fmt <= '9'; ++fmt)
                precision = precision * 10 + (*fmt - '0');
        }
        for (; *fmt == 'l' || *fmt == 'z' || *fmt == 'h'; ++fmt)
            if (*fmt != 'h')
                ++longs;
        switch (*fmt)
        {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'p':
            {
                unsigned long long v;
                int neg = 0;
                int base = (*fmt == 'x' || *fmt == 'X' || *fmt == 'p') ? 16 : 10;
                const char *digits = *fmt == 'X' ? "0123456789ABCDEF" : "0123456789abcdef";
                char rev[32];
                size_t k = 0;
                if (*fmt == 'p')
                    v = (uintptr_t)va_arg(ap, void *);
                else if (*fmt == 'd' || *fmt == 'i')
                {
                    long long sv = longs ? va_arg(ap, long long) : va_arg(ap, int);
                    neg = sv < 0;
                    v = neg ? (unsigned long long)-sv : (unsigned long long)sv;
                }
                else
                    v = longs ? va_arg(ap, unsigned long long) : va_arg(ap, unsigned int);
                do
                {
                    rev[k++] = digits[v % (unsigned)base];
                    v /= (unsigned)base;
                } while (v != 0);
                if (neg)
                    tmp[n++] = '-';
                while (k > 0)
                    tmp[n++] = rev[--k];
            }
            break;
        case 'f':
        case 'g':
            {
                double v = va_arg(ap, double);
                unsigned long long whole;
                double frac;
                char rev[32];
                size_t k = 0;
                int p = precision < 0 ? 6 : precision;
                if (*fmt == 'g' && precision < 0)
                    p = 4;
                if (v < 0)
                {
                    tmp[n++] = '-';
                    v = -v;
                }
                {
                    double r = 0.5;
                    int i;
                    for (i = 0; i < p; ++i)
                        r /= 10.0;
                    v += r;
                }
                whole = (unsigned long long)v;
                frac = v - (double)whole;
                do
                {
                    rev[k++] = (char)('0' + whole % 10);
                    whole /= 10;
                } while (whole != 0);
                while (k > 0)
                    tmp[n++] = rev[--k];
                if (p > 0 && n < sizeof(tmp) - 2)
                {
                    tmp[n++] = '.';
                    while (p-- > 0 && n < sizeof(tmp) - 1)
                    {
                        int d;
                        frac *= 10.0;
                        d = (int)frac;
                        tmp[n++] = (char)('0' + d);
                        frac -= d;
                    }
                }
            }
            break;
        case 'c':
            tmp[n++] = (char)va_arg(ap, int);
            break;
        case 's':
            s = va_arg(ap, const char *);
            if (s == NULL)
                s = "(null)";
            n = strlen(s);
            if (precision >= 0 && (size_t)precision < n)
                n = (size_t)precision;
            fill = ' ';
            break;
        case '%':
            tmp[n++] = '%';
            break;
        default:
            --fmt;
            break;
        }
        fmt_pad(&o, s, n, width, left, fill);
        if (*fmt == '\0')
            break;
    }
    if (o.size > 0)
        o.buf[o.len < o.size ? o.len : o.size - 1] = '\0';
    return (int)o.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

/* --- files: what the kernel handed over, by path */

struct moscow_file
{
    const unsigned char *data;
    long size;
    long at;
    int eof;
    int stream;     /* 1: stderr or stdout */
};

typedef struct gui_file
{
    char *path;
    unsigned char *data;
    long size;
    struct gui_file *next;
} gui_file_t;

static gui_file_t *gui_files;
static struct moscow_file gui_stderr = { NULL, 0, 0, 0, 1 };
FILE *stderr = &gui_stderr;
FILE *stdout = &gui_stderr;

/* the last lines written to stderr, for the kernel to show */
static char gui_messages[1024];

static void serial_put(char c)
{
    int spin = 100000;
    while ((port_in8(0x3f8 + 5) & 0x20) == 0 && --spin > 0)
        ;
    port_out8(0x3f8, (uint8_t)c);
}

int fprintf(FILE *f, const char *fmt, ...)
{
    char text[512];
    va_list ap;
    int n;
    size_t have;
    size_t add;
    size_t i;

    va_start(ap, fmt);
    n = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (f == NULL || !f->stream)
        return n;
    for (i = 0; text[i] != '\0'; ++i)
    {
        if (text[i] == '\n')
            serial_put('\r');
        serial_put(text[i]);
    }
    have = strlen(gui_messages);
    add = strlen(text);
    if (have + add >= sizeof(gui_messages))
        have = 0;
    memcpy(gui_messages + have, text, add + 1);
    return n;
}

int printf(const char *fmt, ...)
{
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    return fprintf(stdout, "%s", text);
}

/* the kernel hands a file over: path and its bytes; 0, or -1 */
long gui_file_put(const char *path, const unsigned char *data, long size)
{
    gui_file_t *f;

    for (f = gui_files; f != NULL; f = f->next)
        if (strcmp(f->path, path) == 0)
            break;
    if (f == NULL)
    {
        f = calloc(1, sizeof(*f));
        if (f == NULL)
            return -1;
        f->path = malloc(strlen(path) + 1);
        if (f->path == NULL)
        {
            free(f);
            return -1;
        }
        strcpy(f->path, path);
        f->next = gui_files;
        gui_files = f;
    }
    free(f->data);
    f->data = malloc(size > 0 ? (size_t)size : 1);
    if (f->data == NULL)
    {
        f->size = 0;
        return -1;
    }
    memcpy(f->data, data, (size_t)size);
    f->size = size;
    return 0;
}

/* 1 when the kernel has handed path over */
long gui_file_has(const char *path)
{
    gui_file_t *f;
    for (f = gui_files; f != NULL; f = f->next)
        if (strcmp(f->path, path) == 0)
            return 1;
    return 0;
}

FILE *fopen(const char *path, const char *mode)
{
    gui_file_t *f;
    FILE *file;

    if (mode[0] != 'r')
        return NULL;
    for (f = gui_files; f != NULL; f = f->next)
        if (strcmp(f->path, path) == 0)
            break;
    if (f == NULL)
    {
        errno = ENOENT;
        return NULL;
    }
    file = calloc(1, sizeof(*file));
    if (file == NULL)
        return NULL;
    file->data = f->data;
    file->size = f->size;
    return file;
}

int fclose(FILE *f)
{
    if (f != NULL && !f->stream)
        free(f);
    return 0;
}

size_t fread(void *p, size_t size, size_t count, FILE *f)
{
    size_t want = size * count;
    size_t left = (size_t)(f->size - f->at);
    if (size == 0)
        return 0;
    if (want > left)
    {
        want = left - left % size;
        f->eof = 1;
    }
    memcpy(p, f->data + f->at, want);
    f->at += (long)want;
    return want / size;
}

int fseek(FILE *f, long offset, int whence)
{
    long at = whence == SEEK_SET ? offset :
              whence == SEEK_CUR ? f->at + offset : f->size + offset;
    if (at < 0 || at > f->size)
        return -1;
    f->at = at;
    f->eof = 0;
    return 0;
}

long ftell(FILE *f) { return f->at; }

int fgetc(FILE *f)
{
    if (f->at >= f->size)
    {
        f->eof = 1;
        return EOF;
    }
    return f->data[f->at++];
}

int ungetc(int c, FILE *f)
{
    if (c == EOF || f->at == 0)
        return EOF;
    --f->at;
    f->eof = 0;
    return c;
}

int feof(FILE *f) { return f->eof; }

/* --- numbers */

double floor(double x)
{
    long long i;
    if (!(x > -4503599627370496.0 && x < 4503599627370496.0))
        return x;
    i = (long long)x;
    if ((double)i > x)
        --i;
    return (double)i;
}

double ceil(double x) { return -floor(-x); }
double fabs(double x) { return x < 0 ? -x : x; }
float floorf(float x) { return (float)floor(x); }
float ceilf(float x) { return (float)ceil(x); }
float fabsf(float x) { return x < 0 ? -x : x; }

double sqrt(double x)
{
    double r;
    __asm__ ("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}

float sqrtf(float x)
{
    float r;
    __asm__ ("sqrtss %1, %0" : "=x"(r) : "x"(x));
    return r;
}

double fmod(double x, double y)
{
    double q;
    if (y == 0.0)
        return 0.0;
    q = x / y;
    q = q < 0 ? ceil(q) : floor(q);
    return x - q * y;
}

/* sin on [-pi/4, pi/4] and cos likewise, then by quadrant */
static double sin_kernel(double x)
{
    double x2 = x * x;
    return x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0 *
           (1.0 - x2 / 72.0 * (1.0 - x2 / 110.0 * (1.0 - x2 / 156.0))))));
}

static double cos_kernel(double x)
{
    double x2 = x * x;
    return 1.0 - x2 / 2.0 * (1.0 - x2 / 12.0 * (1.0 - x2 / 30.0 *
           (1.0 - x2 / 56.0 * (1.0 - x2 / 90.0 * (1.0 - x2 / 132.0)))));
}

double sin(double x)
{
    double q = floor(x / (M_PI / 2.0) + 0.5);
    double r = x - q * (M_PI / 2.0);
    long k = (long)q & 3;
    if (k < 0)
        k += 4;
    switch (k)
    {
    case 0: return sin_kernel(r);
    case 1: return cos_kernel(r);
    case 2: return -sin_kernel(r);
    default: return -cos_kernel(r);
    }
}

double cos(double x) { return sin(x + M_PI / 2.0); }
