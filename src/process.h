#ifndef USSR_PROCESS_H
#define USSR_PROCESS_H

#include <stddef.h>

/*
 * Platform-independent child-process interface.
 *
 * The caller owns argv and all strings referenced by it.
 * process_run() does not modify argv.
 *
 * stdin_data may be NULL for an empty standard input.
 * If capture_stdout is non-zero, stdout_data must be non-NULL and
 * receives a newly allocated NUL-terminated buffer.  The caller owns it.
 */
typedef struct
{
    const char *program;
    char *const *argv;
    const char *stdin_data;
    char **stdout_data;
    int capture_stdout;
} ussr_process_t;

/* Returns 0 when the process was launched and waited successfully.
 * exit_code receives the child's normal exit code, or 126 when the
 * executable could not be launched.
 */
int ussr_process_run(
    const ussr_process_t *process,
    int *exit_code
);

#endif /* USSR_PROCESS_H */
