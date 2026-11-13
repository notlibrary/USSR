/*
 * ctest.c - a small C library for the C interface's tests: built
 * freestanding into an ELF shared object for Moscow (moscow.mk puts it in
 * /lib/ctest.so), it exercises what the loader does: arguments in
 * registers and on the stack, imports from Moscow's C runtime (strlen,
 * memset), pointers in its data that need relocating, a constructor.
 * Loaded by a program it runs in ring 3: ctest_peek and ctest_poke at the
 * kernel's memory, and ctest_cli, are stopped there (moscow_ring3.su).
 */
#include <stddef.h>

size_t strlen(const char *s);
void *memset(void *p, int c, size_t n);
long moscow_ticks(void);

/* how often the constructor ran: once, when the library was loaded */
int ctest_starts;
/* pointers in its data: the loader relocates them (R_X86_64_RELATIVE) */
const char *ctest_names[] = { "zero", "one", "two" };

__attribute__((constructor)) static void ctest_start(void)
{
    /* an import, so the compiler cannot run it at compile time */
    ctest_starts += (int)strlen(ctest_names[1]) - 2;
}

int ctest_constructed(void) { return ctest_starts; }

long ctest_add(long a, long b) { return a + b; }

long ctest_twelve(long a, long b, long c, long d, long e, long f,
                  long g, long h, long i, long j, long k, long l)
{
    /* weights tell the arguments apart */
    return a + 2 * b + 3 * c + 4 * d + 5 * e + 6 * f + 7 * g + 8 * h +
           9 * i + 10 * j + 11 * k + 12 * l;
}

size_t ctest_length(const char *s) { return strlen(s); }

/* fills buf[0 .. n) with v, then puts n at the end of it */
void ctest_fill(unsigned char *buf, int n, int v)
{
    memset(buf, v, (size_t)n);
    buf[n] = (unsigned char)n;
}

const char *ctest_name(int i) { return i >= 0 && i < 3 ? ctest_names[i] : "none"; }

/* the timer, from Moscow (int 0x81 in ring 3) */
long ctest_ticks(void) { return moscow_ticks(); }

/* memory at any address: its own is fine, the kernel's is not ring 3's */
long ctest_peek(long addr) { return *(volatile long *)addr; }
void ctest_poke(long addr, long v) { *(volatile long *)addr = v; }

/* an instruction only the kernel may run */
void ctest_cli(void) { __asm__ volatile("cli"); }
