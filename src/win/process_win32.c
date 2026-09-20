#include "process.h"

#ifdef _WIN32

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *quote_argument(const char *argument)
{
    size_t i;
    size_t length;
    size_t extra;
    char *result;
    char *out;
    int quote;

    if (argument == NULL)
        argument = "";

    quote = (*argument == '\0');
    for (i = 0; argument[i] != '\0'; ++i)
    {
        if (argument[i] == ' ' || argument[i] == '\t' ||
            argument[i] == '"')
        {
            quote = 1;
            break;
        }
    }

    length = strlen(argument);
    extra = quote ? 2 : 0;

    for (i = 0; i < length; ++i)
    {
        if (argument[i] == '"' || argument[i] == '\\')
            ++extra;
    }

    result = malloc(length + extra + 1);
    if (result == NULL)
        return NULL;

    out = result;

    if (quote)
        *out++ = '"';

    for (i = 0; i < length; ++i)
    {
        char c = argument[i];

        if (c == '"')
        {
            *out++ = '\\';
            *out++ = '"';
        }
        else
        {
            *out++ = c;
        }
    }

    if (quote)
        *out++ = '"';

    *out = '\0';
    return result;
}

static char *build_command_line(char *const *argv)
{
    size_t i;
    size_t total;
    char *line;
    char *cursor;

    total = 1;

    for (i = 0; argv[i] != NULL; ++i)
    {
        char *quoted = quote_argument(argv[i]);
        if (quoted == NULL)
            return NULL;
        total += strlen(quoted) + 1;
        free(quoted);
    }

    line = malloc(total);
    if (line == NULL)
        return NULL;

    cursor = line;
    *cursor = '\0';

    for (i = 0; argv[i] != NULL; ++i)
    {
        char *quoted = quote_argument(argv[i]);
        size_t length;

        if (quoted == NULL)
        {
            free(line);
            return NULL;
        }

        if (i != 0)
            *cursor++ = ' ';

        length = strlen(quoted);
        memcpy(cursor, quoted, length);
        cursor += length;
        *cursor = '\0';
        free(quoted);
    }

    return line;
}

static int make_temp_file(
    HANDLE *handle,
    char *path,
    DWORD path_size
)
{
    char temp_path[MAX_PATH];
    UINT result;

    if (handle == NULL || path == NULL || path_size == 0 ||
        path_size < MAX_PATH)
        return -1;

    if (GetTempPathA(sizeof(temp_path), temp_path) == 0)
        return -1;

    result = GetTempFileNameA(temp_path, "ussr", 0, path);
    if (result == 0)
        return -1;

    *handle = CreateFileA(
        path,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_TEMPORARY,
        NULL
    );

    if (*handle == INVALID_HANDLE_VALUE)
    {
        DeleteFileA(path);
        return -1;
    }

    return 0;
}

static int write_input(HANDLE handle, const char *data)
{
    DWORD written;
    size_t length;
    size_t offset;

    if (data == NULL)
        return 0;

    length = strlen(data);
    offset = 0;

    while (offset < length)
    {
        DWORD chunk = (DWORD)((length - offset) > 0x7fffffffU
            ? 0x7fffffffU
            : length - offset);

        if (!WriteFile(
                handle,
                data + offset,
                chunk,
                &written,
                NULL
            ))
            return -1;

        if (written == 0)
            return -1;

        offset += written;
    }

    return SetFilePointer(handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER
        && GetLastError() != NO_ERROR ? -1 : 0;
}

static int read_output(HANDLE handle, char **output)
{
    LARGE_INTEGER size;
    char *buffer;
    DWORD read_count;
    size_t length;
    size_t capacity;

    if (output == NULL)
        return -1;

    *output = NULL;

    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0)
        return -1;

    if ((unsigned long long)size.QuadPart > (unsigned long long)SIZE_MAX - 1)
        return -1;

    capacity = (size_t)size.QuadPart + 1;
    buffer = malloc(capacity == 0 ? 1 : capacity);
    if (buffer == NULL)
        return -1;

    if (SetFilePointer(handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER &&
        GetLastError() != NO_ERROR)
    {
        free(buffer);
        return -1;
    }

    length = 0;

    while (length < (size_t)size.QuadPart)
    {
        DWORD requested = (DWORD)(((size_t)size.QuadPart - length) > 0x7fffffffU
            ? 0x7fffffffU
            : (size_t)size.QuadPart - length);

        if (!ReadFile(
                handle,
                buffer + length,
                requested,
                &read_count,
                NULL
            ))
        {
            free(buffer);
            return -1;
        }

        if (read_count == 0)
            break;

        length += read_count;
    }

    buffer[length] = '\0';
    *output = buffer;
    return 0;
}

int ussr_process_run(
    const ussr_process_t *process,
    int *exit_code
)
{
    STARTUPINFOA startup;
    PROCESS_INFORMATION child;
    SECURITY_ATTRIBUTES security;
    HANDLE stdin_file;
    HANDLE stdout_file;
    char stdin_path[MAX_PATH];
    char stdout_path[MAX_PATH];
    char *command_line;
    DWORD process_exit;
    int result;

    if (process == NULL || process->program == NULL ||
        process->argv == NULL || exit_code == NULL)
        return -1;

    if (process->capture_stdout && process->stdout_data == NULL)
        return -1;

    if (process->stdout_data != NULL)
        *process->stdout_data = NULL;

    memset(&startup, 0, sizeof(startup));
    memset(&child, 0, sizeof(child));
    memset(&security, 0, sizeof(security));

    stdin_file = INVALID_HANDLE_VALUE;
    stdout_file = INVALID_HANDLE_VALUE;
    stdin_path[0] = '\0';
    stdout_path[0] = '\0';

    if (process->stdin_data != NULL)
    {
        if (make_temp_file(&stdin_file, stdin_path, sizeof(stdin_path)) != 0)
            return -1;

        if (write_input(stdin_file, process->stdin_data) != 0)
        {
            CloseHandle(stdin_file);
            DeleteFileA(stdin_path);
            return -1;
        }
    }

    if (process->capture_stdout)
    {
        if (make_temp_file(&stdout_file, stdout_path, sizeof(stdout_path)) != 0)
        {
            if (stdin_file != INVALID_HANDLE_VALUE)
            {
                CloseHandle(stdin_file);
                DeleteFileA(stdin_path);
            }
            return -1;
        }
    }

    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = stdin_file != INVALID_HANDLE_VALUE
        ? stdin_file
        : GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = stdout_file != INVALID_HANDLE_VALUE
        ? stdout_file
        : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    command_line = build_command_line(process->argv);
    if (command_line == NULL)
    {
        if (stdin_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(stdin_file);
            DeleteFileA(stdin_path);
        }
        if (stdout_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(stdout_file);
            DeleteFileA(stdout_path);
        }
        return -1;
    }

    result = CreateProcessA(
        process->program,
        command_line,
        &security,
        NULL,
        TRUE,
        0,
        NULL,
        NULL,
        &startup,
        &child
    );

    free(command_line);

    if (!result)
    {
        if (stdin_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(stdin_file);
            DeleteFileA(stdin_path);
        }
        if (stdout_file != INVALID_HANDLE_VALUE)
        {
            CloseHandle(stdout_file);
            DeleteFileA(stdout_path);
        }
        return -1;
    }

    CloseHandle(child.hThread);

    if (stdin_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(stdin_file);
        stdin_file = INVALID_HANDLE_VALUE;
    }

    if (stdout_file != INVALID_HANDLE_VALUE)
    {
        CloseHandle(stdout_file);
        stdout_file = INVALID_HANDLE_VALUE;
    }

    WaitForSingleObject(child.hProcess, INFINITE);

    if (!GetExitCodeProcess(child.hProcess, &process_exit))
    {
        CloseHandle(child.hProcess);
        DeleteFileA(stdin_path);
        DeleteFileA(stdout_path);
        return -1;
    }

    CloseHandle(child.hProcess);

    if (process->capture_stdout)
    {
        HANDLE output_handle = CreateFileA(
            stdout_path,
            GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            NULL
        );

        if (output_handle == INVALID_HANDLE_VALUE ||
            read_output(output_handle, process->stdout_data) != 0)
        {
            if (output_handle != INVALID_HANDLE_VALUE)
                CloseHandle(output_handle);
            DeleteFileA(stdin_path);
            DeleteFileA(stdout_path);
            return -1;
        }

        CloseHandle(output_handle);
    }

    DeleteFileA(stdin_path);
    DeleteFileA(stdout_path);

    *exit_code = (int)process_exit;
    return 0;
}

#endif /* _WIN32 */
