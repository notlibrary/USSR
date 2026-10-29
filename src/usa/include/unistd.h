#ifndef _UNISTD_H
#define _UNISTD_H
#include <stddef.h>
typedef long ssize_t;
int isatty(int fd);
int unlink(const char *path);
int access(const char *path, int mode);
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif
