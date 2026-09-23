#include "completion_fs.h"

#ifndef _WIN32

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

        if (dst[n - 1] != '/')
        {
            dst[n++] = '/';
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
    const char *slash;
    size_t directory_length;
    size_t prefix_length;
    size_t count = 0;
    DIR *dir;
    struct dirent *entry;

    if (partial == NULL || entries == NULL || capacity == 0)
        return 0;

    slash = strrchr(partial, '/');

    if (slash == NULL)
    {
        strcpy(directory, ".");
        strncpy(
            name_prefix,
            partial,
            sizeof(name_prefix) - 1
        );
        name_prefix[sizeof(name_prefix) - 1] = '\0';
    }
    else
    {
        prefix_length = (size_t)(slash - partial) + 1;

        if (prefix_length >= sizeof(directory))
            return 0;

        memcpy(directory, partial, prefix_length);
        directory[prefix_length] = '\0';

        strncpy(
            name_prefix,
            slash + 1,
            sizeof(name_prefix) - 1
        );
        name_prefix[sizeof(name_prefix) - 1] = '\0';
    }

    directory_length = strlen(directory);
    (void)directory_length;

    dir = opendir(directory);
    if (dir == NULL)
        return 0;

    while (count < capacity && (entry = readdir(dir)) != NULL)
    {
        char full[USSR_COMPLETION_FS_MAX_PATH];
        struct stat st;

        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0)
            continue;

        if (strncmp(
                entry->d_name,
                name_prefix,
                strlen(name_prefix)) != 0)
            continue;

        join_path(full, sizeof(full), directory, entry->d_name);

        if (stat(full, &st) != 0)
            continue;

        strncpy(entries[count].path, full, sizeof(entries[count].path) - 1);
        entries[count].path[sizeof(entries[count].path) - 1] = '\0';
        entries[count].directory = S_ISDIR(st.st_mode) != 0;
        entries[count].executable =
            !entries[count].directory &&
            access(full, X_OK) == 0;
        ++count;
    }

    closedir(dir);
    return count;
}

#endif
