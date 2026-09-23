#ifndef USSR_COMPLETION_FS_H
#define USSR_COMPLETION_FS_H

#include <stddef.h>

#define USSR_COMPLETION_FS_MAX_RESULTS 128
#define USSR_COMPLETION_FS_MAX_PATH 4096

typedef struct
{
    char path[USSR_COMPLETION_FS_MAX_PATH];
    int directory;
    int executable;
} ussr_completion_fs_entry_t;

/*
 * List filesystem entries matching a partial path.
 *
 * The returned paths are ready to be handed to Bestline as completion
 * strings.  directory is non-zero for directories; executable is a
 * best-effort executable hint used by the empirical ranker.
 */
size_t ussr_completion_fs_list(
    const char *partial,
    ussr_completion_fs_entry_t *entries,
    size_t capacity
);

#endif
