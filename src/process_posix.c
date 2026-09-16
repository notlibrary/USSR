#include "process.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

static int ussr_command_is_executable(const char *path)
{
    if (path == NULL)
        return 0;

    return access(path, X_OK) == 0;
}

char *ussr_find_external_command(const char *command)
{
    const char *path;
    const char *start;
    const char *end;
    size_t directory_length;
    size_t command_length;
    size_t length;
    char *candidate;

    if (command == NULL || *command == '\0')
        return NULL;

    /*
     * A command containing '/' is already a path.
     */
    if (strchr(command, '/') != NULL)
    {
        if (ussr_command_is_executable(command))
            return strdup(command);

        return NULL;
    }

    /*
     * Search PATH first.
     */
    path = getenv("PATH");

    if (path != NULL)
    {
        start = path;
        command_length = strlen(command);

        while (*start != '\0')
        {
            end = strchr(start, ':');

            if (end == NULL)
                end = start + strlen(start);

            directory_length = (size_t)(end - start);

            /*
             * Empty PATH component means current directory.
             */
            if (directory_length == 0)
            {
                length = 2 + command_length;

                candidate = malloc(length);

                if (candidate == NULL)
                    return NULL;

                snprintf(
                    candidate,
                    length,
                    "./%s",
                    command
                );
            }
            else
            {
                length =
                    directory_length +
                    1 +
                    command_length +
                    1;

                candidate = malloc(length);

                if (candidate == NULL)
                    return NULL;

                snprintf(
                    candidate,
                    length,
                    "%.*s/%s",
                    (int)directory_length,
                    start,
                    command
                );
            }

            if (ussr_command_is_executable(candidate))
                return candidate;

            free(candidate);

            if (*end == '\0')
                break;

            start = end + 1;
        }
    }

    /*
     * Finally check the current working directory explicitly.
     */
    length = 2 + strlen(command);

    candidate = malloc(length);

    if (candidate == NULL)
        return NULL;

    snprintf(candidate, length, "./%s", command);

    if (ussr_command_is_executable(candidate))
        return candidate;

    free(candidate);

    return NULL;
}

int ussr_run_process(char *const argv[], long *exit_code)
{
    pid_t pid;
    int status;

    pid = fork();

    if (pid < 0)
    {
        perror("USSR: fork");
        return -1;
    }

    if (pid == 0)
    {
        execv(argv[0], argv);

        /*
         * Only reached when execv() fails.
         */
        fprintf(
            stderr,
            "USSR: cannot execute '%s': %s\n",
            argv[0],
            strerror(errno)
        );

        _exit(126);
    }

    do
    {
        if (waitpid(pid, &status, 0) < 0)
        {
            if (errno == EINTR)
                continue;

            perror("USSR: waitpid");
            return -1;
        }

        break;
    }
    while (1);

    /*
     * If the process was killed by a signal, use 128 + signal in
     * the same general convention used by shells.
     */
    if (WIFEXITED(status))
        *exit_code = (long)WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        *exit_code = 128L + (long)WTERMSIG(status);
    else
        *exit_code = -1;

    return 0;
}
