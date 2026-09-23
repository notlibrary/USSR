#include "completion_fs.h"

#ifdef _WIN32

#include <stdio.h>
#include <string.h>
#include <windows.h>

static void join_path(
    char *dst,
    size_t size,
    const char *prefix,
    const char *name)
{
    size_t n;

    if (size == 0)
        return;

    dst[0] = '\0';

    if (prefix != NULL && prefix[0] != '\0')
    {
        n = strlen(prefix);
        if (n + 1 >= size)
            return;

        memcpy(dst, prefix, n);
        dst[n] = '\0';

        if (dst[n - 1] != '/' && dst[n - 1] != '\\')
        {
            dst[n++] = '\\';
            dst[n] = '\0';
        }
    }

    if (name != NULL)
        strncat(dst, name, size - strlen(dst) - 1);
}

size_t ussr_completion_fs_list(
    const char *partial,
    ussr_completion_fs_entry_t *entries,
    size_t capacity)
{
    char directory[USSR_COMPLETION_FS_MAX_PATH];
    char name_prefix[USSR_COMPLETION_FS_MAX_PATH];
    char pattern[USSR_COMPLETION_FS_MAX_PATH];
    const char *slash1;
    const char *slash2;
    const char *slash;
    size_t prefix_length;
    size_t count = 0;
    HANDLE handle;
    WIN32_FIND_DATAA data;

    if (partial == NULL || entries == NULL || capacity == 0)
        return 0;

    slash1 = strrchr(partial, '/');
    slash2 = strrchr(partial, '\\');
    slash = slash1;

    if (slash2 != NULL && (slash == NULL || slash2 > slash))
        slash = slash2;

    if (slash == NULL)
    {
        strcpy(directory, ".");
        strncpy(name_prefix, partial, sizeof(name_prefix) - 1);
        name_prefix[sizeof(name_prefix) - 1] = '\0';
    }
    else
    {
        prefix_length = (size_t)(slash - partial) + 1;
        if (prefix_length >= sizeof(directory))
            return 0;

        memcpy(directory, partial, prefix_length);
        directory[prefix_length] = '\0';

        strncpy(name_prefix, slash + 1, sizeof(name_prefix) - 1);
        name_prefix[sizeof(name_prefix) - 1] = '\0';
    }

    snprintf(
        pattern,
        sizeof(pattern),
        "%s%s*",
        directory,
        directory[0] != '\0' &&
        (directory[strlen(directory) - 1] == '/' ||
         directory[strlen(directory) - 1] == '\\') ? "" : "\\"
    );

    handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE)
        return 0;

    do
    {
        char full[USSR_COMPLETION_FS_MAX_PATH];
        int is_directory;

        if (strcmp(data.cFileName, ".") == 0 ||
            strcmp(data.cFileName, "..") == 0)
            continue;

        if (_strnicmp(
                data.cFileName,
                name_prefix,
                strlen(name_prefix)) != 0)
            continue;

        is_directory =
            (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;

        join_path(full, sizeof(full), directory, data.cFileName);

        strncpy(entries[count].path, full, sizeof(entries[count].path) - 1);
        entries[count].path[sizeof(entries[count].path) - 1] = '\0';
        entries[count].directory = is_directory;
        entries[count].executable = !is_directory;
        ++count;
    } while (count < capacity && FindNextFileA(handle, &data));

    FindClose(handle);
    return count;
}

#endif
