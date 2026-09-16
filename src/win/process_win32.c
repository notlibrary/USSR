#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "../process.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------- */
/* PATH / PATHEXT search                                           */
/* ------------------------------------------------------------- */

static int ussr_file_exists(const char *path)
{
    DWORD attrs;

    if (path == NULL)
        return 0;

    attrs = GetFileAttributesA(path);

    return attrs != INVALID_FILE_ATTRIBUTES &&
           (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static int ussr_has_known_extension(const char *name)
{
    /* True if `name` already ends in one of PATHEXT's extensions
     * (or any "*.something" at all -- if the caller wrote an
     * explicit extension, don't second-guess it by also trying
     * PATHEXT suffixes on top of it). */
    return strrchr(name, '.') != NULL;
}

/*
 * Tries `base` (a full path with no extension yet, or a bare command
 * name) against every ';'-separated suffix in PATHEXT (or a sane
 * default if PATHEXT isn't set), the same way cmd.exe resolves a
 * command with no extension. Returns a newly malloc'd match, or NULL.
 */
static char *ussr_try_extensions(const char *base)
{
    const char *pathext = getenv("PATHEXT");
    const char *default_pathext = ".COM;.EXE;.BAT;.CMD";
    const char *start;
    const char *end;
    char *candidate;
    size_t base_length = strlen(base);

    if (pathext == NULL || pathext[0] == '\0')
        pathext = default_pathext;

    start = pathext;

    while (*start != '\0')
    {
        size_t ext_length;

        end = strchr(start, ';');
        if (end == NULL)
            end = start + strlen(start);

        ext_length = (size_t)(end - start);

        if (ext_length > 0)
        {
            candidate = malloc(base_length + ext_length + 1);
            if (candidate == NULL)
                return NULL;

            memcpy(candidate, base, base_length);
            memcpy(candidate + base_length, start, ext_length);
            candidate[base_length + ext_length] = '\0';

            if (ussr_file_exists(candidate))
                return candidate;

            free(candidate);
        }

        if (*end == '\0')
            break;
        start = end + 1;
    }

    return NULL;
}

/* Tries `base` as-is (if it already has an extension) or with each
 * PATHEXT suffix appended (if it doesn't). Returns a newly malloc'd
 * match, or NULL. */
static char *ussr_resolve_candidate(const char *base)
{
    if (ussr_has_known_extension(base))
    {
        if (ussr_file_exists(base))
            return _strdup(base);
        return NULL;
    }

    return ussr_try_extensions(base);
}

char *ussr_find_external_command(const char *command)
{
    const char *path;
    const char *start;
    const char *end;
    char *result;
    char *candidate;

    if (command == NULL || *command == '\0')
        return NULL;

    /*
     * A command containing a path separator is already a path --
     * accept either slash direction, since Win32 APIs (unlike
     * cmd.exe's own parser) handle '/' fine.
     */
    if (strchr(command, '/') != NULL || strchr(command, '\\') != NULL)
        return ussr_resolve_candidate(command);

    /*
     * Current directory first (matches the POSIX build's own
     * explicit "./command" check, and how a real shell prompt
     * behaves on Windows for a bare filename).
     */
    result = ussr_resolve_candidate(command);
    if (result != NULL)
        return result;

    /*
     * Then PATH, ';'-separated.
     */
    path = getenv("PATH");
    if (path == NULL)
        return NULL;

    start = path;

    while (*start != '\0')
    {
        size_t directory_length;
        char *base;

        end = strchr(start, ';');
        if (end == NULL)
            end = start + strlen(start);

        directory_length = (size_t)(end - start);

        if (directory_length > 0)
        {
            size_t command_length = strlen(command);

            base = malloc(directory_length + 1 + command_length + 1);
            if (base == NULL)
                return NULL;

            memcpy(base, start, directory_length);
            base[directory_length] = '\\';
            memcpy(base + directory_length + 1, command, command_length + 1);

            candidate = ussr_resolve_candidate(base);
            free(base);

            if (candidate != NULL)
                return candidate;
        }

        if (*end == '\0')
            break;
        start = end + 1;
    }

    return NULL;
}

/* ------------------------------------------------------------- */
/* argv[] -> one Win32 command-line string, MSVCRT-compatible      */
/* ------------------------------------------------------------- */

/*
 * Windows processes don't receive argv[] from the OS -- CreateProcess
 * takes one command-line string, and the CHILD's C runtime re-splits
 * it back into argv[] using MSVCRT's own quoting rules. Building that
 * string wrong is a classic, silent source of bugs (arguments with
 * spaces or trailing backslashes get mis-split). This follows
 * Microsoft's own documented algorithm for it.
 */
static int ussr_append_quoted_argument(
    char **buffer,
    size_t *length,
    size_t *capacity,
    const char *argument
)
{
    size_t needed;
    size_t backslashes;
    const char *p;
    int needs_quotes =
        argument[0] == '\0' || strpbrk(argument, " \t\n\v\"") != NULL;

    /* Worst case: every char doubled plus two quotes plus a space
     * separator plus the null terminator -- generous but simple. */
    needed = *length + 2 * strlen(argument) + 4;

    if (needed > *capacity)
    {
        char *grown = realloc(*buffer, needed * 2);
        if (grown == NULL)
            return -1;
        *buffer = grown;
        *capacity = needed * 2;
    }

    if (*length > 0)
        (*buffer)[(*length)++] = ' ';

    if (!needs_quotes)
    {
        size_t arg_length = strlen(argument);
        memcpy(*buffer + *length, argument, arg_length);
        *length += arg_length;
        (*buffer)[*length] = '\0';
        return 0;
    }

    (*buffer)[(*length)++] = '"';

    for (p = argument; *p != '\0'; ++p)
    {
        if (*p == '\\')
        {
            backslashes = 0;
            while (*p == '\\')
            {
                ++backslashes;
                ++p;
            }

            if (*p == '"' || *p == '\0')
                backslashes *= 2; /* escape every backslash before a quote (or end) */

            while (backslashes-- > 0)
                (*buffer)[(*length)++] = '\\';

            if (*p == '\0')
                break;
        }

        if (*p == '"')
            (*buffer)[(*length)++] = '\\';

        (*buffer)[(*length)++] = *p;
    }

    (*buffer)[(*length)++] = '"';
    (*buffer)[*length] = '\0';

    return 0;
}

static char *ussr_build_command_line(char *const argv[])
{
    char *buffer;
    size_t length = 0;
    size_t capacity = 256;
    size_t i;

    buffer = malloc(capacity);
    if (buffer == NULL)
        return NULL;
    buffer[0] = '\0';

    for (i = 0; argv[i] != NULL; ++i)
    {
        if (ussr_append_quoted_argument(&buffer, &length, &capacity, argv[i]) != 0)
        {
            free(buffer);
            return NULL;
        }
    }

    return buffer;
}

/* ------------------------------------------------------------- */
/* run + wait                                                       */
/* ------------------------------------------------------------- */

int ussr_run_process(char *const argv[], long *exit_code)
{
    char *command_line;
    STARTUPINFOA startup_info;
    PROCESS_INFORMATION process_info;
    DWORD wait_result;
    DWORD raw_exit_code;

    command_line = ussr_build_command_line(argv);
    if (command_line == NULL)
    {
        fprintf(stderr, "USSR: out of memory building command line\n");
        return -1;
    }

    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(startup_info);
    ZeroMemory(&process_info, sizeof(process_info));

    /*
     * lpApplicationName is left NULL and the resolved path is just
     * argv[0] inside command_line -- CreateProcess re-resolves it
     * through its own search, which is fine since argv[0] here is
     * already a full path from ussr_find_external_command().
     */
    if (!CreateProcessA(
            NULL,
            command_line,
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            NULL,
            &startup_info,
            &process_info
        ))
    {
        DWORD error = GetLastError();
        fprintf(
            stderr,
            "USSR: cannot execute '%s': Windows error %lu\n",
            argv[0],
            (unsigned long)error
        );
        free(command_line);
        return -1;
    }

    free(command_line);

    wait_result = WaitForSingleObject(process_info.hProcess, INFINITE);

    if (wait_result != WAIT_OBJECT_0)
    {
        fprintf(stderr, "USSR: WaitForSingleObject failed\n");
        CloseHandle(process_info.hProcess);
        CloseHandle(process_info.hThread);
        return -1;
    }

    if (!GetExitCodeProcess(process_info.hProcess, &raw_exit_code))
    {
        fprintf(stderr, "USSR: GetExitCodeProcess failed\n");
        CloseHandle(process_info.hProcess);
        CloseHandle(process_info.hThread);
        return -1;
    }

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);

    /*
     * Windows has no POSIX signals, so there's no exact equivalent of
     * "128 + signal". The closest useful analog: a process killed by
     * an unhandled structured exception (access violation, stack
     * overflow, etc.) exits with an NTSTATUS value as its "exit code",
     * always >= 0xC0000000 (unlike a normal exit code, which a
     * well-behaved process keeps small). Map that whole range to a
     * shell-like 128 + (low byte of the NTSTATUS) so scripts that
     * check for "exit_code > 127 means it died abnormally" behave the
     * same on both platforms, without literally faking a signal
     * number that doesn't exist here.
     */
    if (raw_exit_code >= 0xC0000000UL)
        *exit_code = 128L + (long)(raw_exit_code & 0xFF);
    else
        *exit_code = (long)raw_exit_code;

    return 0;
}
