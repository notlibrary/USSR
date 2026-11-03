/* stdio.h for Moscow's C libraries (lib/gui/libc.c): formatted text into
 * buffers, stderr to the serial port, and files the kernel has handed
 * the library (gui_file_put) read back with fopen. */
#ifndef MOSCOW_STDIO_H
#define MOSCOW_STDIO_H

#include <stddef.h>
#include <stdarg.h>

typedef struct moscow_file FILE;

extern FILE *stderr;
extern FILE *stdout;

#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int printf(const char *fmt, ...);

FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
size_t fread(void *p, size_t size, size_t count, FILE *f);
int fseek(FILE *f, long offset, int whence);
long ftell(FILE *f);
int fgetc(FILE *f);
int ungetc(int c, FILE *f);
int feof(FILE *f);
#define getc fgetc

#endif
