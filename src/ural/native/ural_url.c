#include "ural_url.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char *ural_ftp_url_join(const char *base, const char *leaf)
{
    const char *authority, *path, *path_end;
    size_t authority_length, path_length, leaf_length, length, separator_length;
    char *url, *out;

    if (base == NULL || leaf == NULL)
        return NULL;
    authority = strstr(base, "://");
    if (authority == NULL)
        return NULL;
    authority += 3;
    path = strchr(authority, '/');
    if (path == NULL)
        path = base + strlen(base);
    if (path == authority)
        return NULL;

    authority_length = (size_t)(path - base);
    path_end = base + strlen(base);
    while (path_end > path && path_end[-1] == '/')
        --path_end;
    while (path < path_end && *path == '/')
        ++path;
    path_length = (size_t)(path_end - path);
    leaf_length = strlen(leaf);
    separator_length = path_length != 0 ? 3 : 2;
    if (authority_length > SIZE_MAX - separator_length ||
        path_length > SIZE_MAX - authority_length - separator_length)
        return NULL;
    length = authority_length + separator_length + path_length;
    if (length == SIZE_MAX || leaf_length > SIZE_MAX - length - 1)
        return NULL;
    url = (char *)malloc(length + leaf_length + 1);
    if (url == NULL)
        return NULL;
    out = url;
    memcpy(out, base, authority_length);
    out += authority_length;
    *out++ = '/';
    *out++ = '/';
    if (path_length != 0) {
        memcpy(out, path, path_length);
        out += path_length;
        *out++ = '/';
    }
    memcpy(out, leaf, leaf_length + 1);
    return url;
}
