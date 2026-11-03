/* stdlib.h for Moscow's C libraries (lib/gui/libc.c): the heap is an
 * arena the kernel hands the library at its start. */
#ifndef MOSCOW_STDLIB_H
#define MOSCOW_STDLIB_H

#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t count, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);

int abs(int x);
long labs(long x);
long strtol(const char *s, char **end, int base);
double strtod(const char *s, char **end);
int atoi(const char *s);

#endif
