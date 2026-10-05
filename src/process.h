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

/*
 * Asynchronous child handle used by the scheduler's event queue.
 *
 * The fork-exec model stays intact: ussr_process_spawn() performs the
 * same fork+exec (POSIX) / CreateProcess (Windows) launch as
 * ussr_process_run(), but returns immediately without waiting.  The
 * event queue then watches the handle and reaps the child when it
 * exits, waking whichever USSR process spawned it — so one process
 * running an external command no longer stalls the whole scheduler.
 */
typedef struct
{
#ifdef _WIN32
    void *handle;   /* HANDLE of the child process, NULL when empty */
#else
    int pid;        /* pid_t; <= 0 when empty */
#endif
} ussr_child_t;

/* Launch program with inherited stdin/stdout/stderr, no waiting.
 * Returns 0 and fills child on success. */
int ussr_process_spawn(
    const char *program,
    char *const *argv,
    ussr_child_t *child
);

/* Non-blocking reap: 1 = child exited (exit_code set, handle closed),
 * 0 = still running, -1 = error. */
int ussr_child_poll(ussr_child_t *child, int *exit_code);

/* Blocking wait; reaps and closes the handle. */
int ussr_child_wait(ussr_child_t *child, int *exit_code);

/* Abandon the handle without an exit code (error paths). */
void ussr_child_close(ussr_child_t *child);

#endif /* USSR_PROCESS_H */
