/* usa refine pass 1: C11 and GNU features. Expected output: refine1.txt.
   ussr src/usa/usa.su -o refine1.ru src/usa/tests/refine1.c && ussr refine1.ru */
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

_Static_assert(sizeof(int) == 4, "int is 4 bytes");
#define KIND(x) _Generic((x), int: "int", double: "double", char *: "char*", \
                         unsigned long: "ulong", default: "other")

struct flex { int n; int v[]; };
struct pair { char a; int b; };

static int range(int x)
{
    switch (x) {
    case 1 ... 3: return 10;
    case 4: return 4;
    default: return 0;
    }
}

int main(void)
{
    int x = 3, n = 4, i;
    typeof(x) y = 4;
    __typeof__(y + 1.0) z = 5.5;
    typeof(int *) p = &x;
    char buf[4];
    float f = 1.0f;
    int c0 = __COUNTER__, c1 = __COUNTER__;
    int vla[n];
    struct flex *fl = malloc(sizeof(struct flex) + 3 * sizeof(int));
    printf("typeof: %d %.1f %d\n", x + y, z, *p);
    printf("counter: %d %d\n", c0, c1);
    printf("generic: %s %s %s %s %s %s %d\n", KIND(1), KIND(1.0), KIND(buf),
           KIND(sizeof(int)), KIND(f), KIND('c'), _Generic(x, int: x * 10, default: 0));
    printf("popcount: %d %d %d\n", __builtin_popcount(255u), __builtin_popcountl(0xffffffffffUL),
           __builtin_parity(7u));
    printf("clz/ctz: %d %d %d %d\n", __builtin_clz(1u), __builtin_clzl(1UL), __builtin_ctz(8u),
           __builtin_ctzl(1UL << 40));
    printf("ffs: %d %d\n", __builtin_ffs(0), __builtin_ffs(12));
    printf("bswap: %x %x %lx\n", __builtin_bswap16(0x1234), __builtin_bswap32(0x11223344u),
           __builtin_bswap64(0x0102030405060708UL));
    printf("misc: %ld %d %d\n", __builtin_expect(5L, 1), (int)__builtin_offsetof(struct pair, b),
           (int)_Alignof(double));
    printf("case ranges: %d %d %d\n", range(2), range(4), range(9));
    printf("elvis: %d %d\n", 0 ?: 5, 7 ?: 5);
    for (i = 0; i < n; i++) vla[i] = i * i;
    fl->n = 3;
    fl->v[2] = 9;
    printf("vla: %d %d; flexible member: %d %d\n", vla[3], (int)sizeof vla, (int)sizeof(struct flex), fl->v[2]);
    free(fl);
    return 0;
}
