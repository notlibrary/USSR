#ifndef USSR_PROCESS_H
#define USSR_PROCESS_H

#include <stddef.h>

/*
 * Cross-platform external-command execution, split out of ussr.c so
 * that file has no fork()/execv()/waitpid() (or CreateProcess) in it
 * at all -- it just calls these two functions and stays identical on
 * every platform. Exactly one of process_posix.c / win32's
 * process_win32.c is compiled in, chosen by the Makefile / Makefile.msc.
 */

/*
 * Resolves `command` to a runnable path:
 *   - if it already contains a path separator, checked as-is
 *   - otherwise searched along PATH (':' on POSIX, ';' on Windows,
 *     where the Windows version also tries each PATHEXT suffix)
 * Returns a newly malloc'd absolute/relative path the caller should
 * free, or NULL if nothing runnable was found.
 */
char *ussr_find_external_command(const char *command);

/*
 * Runs argv[0] (already resolved, e.g. via ussr_find_external_command)
 * with the given NULL-terminated argv, waits for it to exit, and
 * reports its status through *exit_code using one shared convention
 * on both platforms: a normal exit reports that exact exit code;
 * abnormal termination reports 128 + signal number on POSIX, or
 * 128 + (NTSTATUS-derived pseudo-signal) on Windows -- see
 * process_win32.c's comment for exactly which codes map to what,
 * since Windows has no POSIX signals to begin with.
 *
 * Returns 0 if the process was launched and waited on at all
 * (regardless of what *exit_code says), or -1 if it couldn't even be
 * started -- a message is already printed to stderr in that case.
 */
int ussr_run_process(char *const argv[], long *exit_code);

#endif
