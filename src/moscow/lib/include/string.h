/* string.h for C libraries Moscow loads: the functions the kernel gives
 * them (src/moscow/arch/amd64/cffi.inc). */
#ifndef MOSCOW_STRING_H
#define MOSCOW_STRING_H

#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *p, int c, size_t n);
int memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);

#endif
