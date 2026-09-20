#include "process.h"

#ifndef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

extern int fileno(FILE *stream);

static int write_all(FILE *file, const char *data)
{
    size_t length;
    size_t written;

    if (file == NULL || data == NULL)
        return 0;

    length = strlen(data);
    written = fwrite(data, 1, length, file);

    if (written != length)
        return -1;

    return fflush(file) == 0 ? 0 : -1;
}

static int read_all(FILE *file, char **result)
{
    char *buffer;
    size_t length;
    size_t capacity;
    size_t n;

    if (file == NULL || result == NULL)
        return -1;

    *result = NULL;
    buffer = NULL;
    length = 0;
    capacity = 0;

    rewind(file);

    for (;;)
    {
        if (length + 4096 + 1 > capacity)
        {
            size_t new_capacity = capacity == 0 ? 4096 : capacity * 2;
            char *new_buffer;

            while (new_capacity < length + 4096 + 1)
                new_capacity *= 2;

            new_buffer = realloc(buffer, new_capacity);
            if (new_buffer == NULL)
            {
                free(buffer);
                return -1;
            }

            buffer = new_buffer;
            capacity = new_capacity;
        }

        n = fread(buffer + length, 1, 4096, file);
        length += n;

        if (n < 4096)
        {
            if (ferror(file))
            {
                free(buffer);
                return -1;
            }
            break;
        }
    }

    if (buffer == NULL)
    {
        buffer = malloc(1);
        if (buffer == NULL)
            return -1;
    }

    buffer[length] = '\0';
    *result = buffer;
    return 0;
}

int ussr_process_run(
    const ussr_process_t *process,
    int *exit_code
)
{
    FILE *input_file;
    FILE *output_file;
    pid_t pid;
    int status;

    if (process == NULL || process->program == NULL ||
        process->argv == NULL || exit_code == NULL)
        return -1;

    if (process->capture_stdout && process->stdout_data == NULL)
        return -1;

    if (process->stdout_data != NULL)
        *process->stdout_data = NULL;

    input_file = NULL;
    output_file = NULL;

    if (process->stdin_data != NULL)
    {
        input_file = tmpfile();
        if (input_file == NULL)
            return -1;

        if (write_all(input_file, process->stdin_data) != 0)
        {
            fclose(input_file);
            return -1;
        }

        rewind(input_file);
    }

    if (process->capture_stdout)
    {
        output_file = tmpfile();
        if (output_file == NULL)
        {
            if (input_file != NULL)
                fclose(input_file);
            return -1;
        }
    }

    pid = fork();

    if (pid < 0)
    {
        if (input_file != NULL)
            fclose(input_file);
        if (output_file != NULL)
            fclose(output_file);
        return -1;
    }

    if (pid == 0)
    {
        if (input_file != NULL)
        {
            if (dup2(fileno(input_file), STDIN_FILENO) < 0)
                _exit(126);
        }

        if (output_file != NULL)
        {
            if (dup2(fileno(output_file), STDOUT_FILENO) < 0)
                _exit(126);
        }

        execv(process->program, process->argv);
        _exit(126);
    }

    if (input_file != NULL)
        fclose(input_file);

    if (output_file != NULL)
        fflush(output_file);

    do
    {
        if (waitpid(pid, &status, 0) < 0)
        {
            if (errno == EINTR)
                continue;

            if (output_file != NULL)
                fclose(output_file);
            return -1;
        }
        break;
    }
    while (1);

    if (output_file != NULL)
    {
        if (read_all(output_file, process->stdout_data) != 0)
        {
            fclose(output_file);
            return -1;
        }
        fclose(output_file);
    }

    if (WIFEXITED(status))
        *exit_code = WEXITSTATUS(status);
    else if (WIFSIGNALED(status))
        *exit_code = 128 + WTERMSIG(status);
    else
        *exit_code = -1;

    return 0;
}

#endif /* !_WIN32 */
