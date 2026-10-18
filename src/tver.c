#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif

#include "tver.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#define tver_casecmp _stricmp
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <strings.h>
#define tver_casecmp strcasecmp
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct
{
    char *name;
    int directory;
    int marked;
} tver_entry_t;

typedef struct
{
    long path_widget;
    long list_widget;
    char path[PATH_MAX];
    tver_entry_t *entries;
    size_t entry_count;
} tver_panel_t;

typedef struct
{
    long window;
    long status;
    tver_panel_t panels[2];
    int active;
    char pending_delete[PATH_MAX * 2 + 128];
} tver_state_t;

static tver_state_t tver_state;

static char *tver_strdup(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL)
        text = "";
    length = strlen(text) + 1;
    copy = malloc(length);
    if (copy != NULL)
        memcpy(copy, text, length);
    return copy;
}

static int tver_expect_integer(
    const ussr_value_t *values,
    size_t count,
    size_t index,
    long *result
)
{
    if (values == NULL || index >= count ||
        values[index].type != USSR_INTEGER)
        return 0;
    *result = values[index].data.integer;
    return 1;
}

static int tver_error_arity(const char *name, size_t actual, size_t expected)
{
    if (actual == expected)
        return 1;
    fprintf(stderr, "USSR: %s expects %lu argument(s), got %lu\n",
            name, (unsigned long)expected, (unsigned long)actual);
    return 0;
}

static int tver_petersburg_call(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *arguments,
    size_t argument_count,
    ussr_value_t *result
)
{
    int status = ussr_petersburg_dispatch(
        program, name, arguments, argument_count, result
    );

    return status == 1;
}

static int tver_set_widget_text(
    ussr_sched_program_t *program,
    long widget,
    const char *text
)
{
    ussr_value_t arguments[2];
    ussr_value_t result = ussr_null();
    int ok;

    arguments[0] = ussr_integer(widget);
    arguments[1] = ussr_string(text);
    if (arguments[1].data.string == NULL)
    {
        ussr_value_free(&arguments[1]);
        return 0;
    }
    ok = tver_petersburg_call(program, "petersburg_set_text",
                              arguments, 2, &result);
    ussr_value_free(&arguments[1]);
    ussr_value_free(&result);
    return ok;
}

static int tver_set_widget_index(
    ussr_sched_program_t *program,
    long widget,
    long index
)
{
    ussr_value_t arguments[2];
    ussr_value_t result = ussr_null();
    int ok;

    arguments[0] = ussr_integer(widget);
    arguments[1] = ussr_integer(index);
    ok = tver_petersburg_call(program, "petersburg_set_index",
                              arguments, 2, &result);
    ussr_value_free(&result);
    return ok;
}

static int tver_get_widget_index(
    ussr_sched_program_t *program,
    long widget,
    long *index
)
{
    ussr_value_t argument = ussr_integer(widget);
    ussr_value_t result = ussr_null();
    int ok = tver_petersburg_call(program, "petersburg_get_index",
                                  &argument, 1, &result);

    if (ok && result.type == USSR_INTEGER)
        *index = result.data.integer;
    else
        ok = 0;
    ussr_value_free(&result);
    return ok;
}

static int tver_set_focus(
    ussr_sched_program_t *program,
    long widget
)
{
    ussr_value_t argument = ussr_integer(widget);
    ussr_value_t result = ussr_null();
    int ok = tver_petersburg_call(program, "petersburg_set_focus",
                                  &argument, 1, &result);

    ussr_value_free(&result);
    return ok;
}

static void tver_set_status(
    ussr_sched_program_t *program,
    const char *text
)
{
    if (tver_state.status != 0 &&
        !tver_set_widget_text(program, tver_state.status, text))
        fprintf(stderr, "USSR: Tver could not update its status label\n");
}

static int tver_entry_compare(const void *left, const void *right)
{
    const tver_entry_t *a = left;
    const tver_entry_t *b = right;

    if (strcmp(a->name, "..") == 0)
        return strcmp(b->name, "..") == 0 ? 0 : -1;
    if (strcmp(b->name, "..") == 0)
        return 1;
    if (a->directory != b->directory)
        return b->directory - a->directory;
    return tver_casecmp(a->name, b->name);
}

static int tver_add_entry(
    tver_panel_t *panel,
    const char *name,
    int directory
)
{
    tver_entry_t *entries;
    char *copy;
    size_t i;

    if (panel->entry_count >= SIZE_MAX / sizeof(*entries))
        return 0;
    copy = tver_strdup(name);
    if (copy == NULL)
        return 0;
    entries = realloc(panel->entries,
                      (panel->entry_count + 1) * sizeof(*entries));
    if (entries == NULL)
    {
        free(copy);
        return 0;
    }
    panel->entries = entries;
    entries = &panel->entries[panel->entry_count];
    entries->name = copy;
    entries->directory = directory;
    entries->marked = 0;
    for (i = 0; i < panel->entry_count; ++i)
    {
        if (strcmp(panel->entries[i].name, name) == 0)
        {
            entries->marked = panel->entries[i].marked;
            break;
        }
    }
    ++panel->entry_count;
    return 1;
}

static int tver_is_root(const char *path)
{
#ifdef _WIN32
    size_t length = strlen(path);
    if (path[0] == '\\' && path[1] == '\\')
    {
        const char *server_end = strpbrk(path + 2, "\\/");
        const char *share_end;
        if (server_end == NULL)
            return 1;
        share_end = strpbrk(server_end + 1, "\\/");
        return share_end == NULL || share_end[1] == '\0';
    }
    return length == 3 && path[1] == ':' &&
           (path[2] == '\\' || path[2] == '/');
#else
    return strcmp(path, "/") == 0;
#endif
}

static int tver_list_directory(tver_panel_t *panel)
{
#ifdef _WIN32
    char pattern[PATH_MAX];
    WIN32_FIND_DATAA data;
    HANDLE handle;
    size_t length = strlen(panel->path);

    if (length + 4 >= sizeof(pattern))
        return 0;
    memcpy(pattern, panel->path, length);
    if (length > 0 && pattern[length - 1] != '\\' &&
        pattern[length - 1] != '/')
        pattern[length++] = '\\';
    memcpy(pattern + length, "*", 2);
    handle = FindFirstFileA(pattern, &data);
    if (handle == INVALID_HANDLE_VALUE)
        return 0;
    if (!tver_is_root(panel->path) &&
        !tver_add_entry(panel, "..", 1))
    {
        FindClose(handle);
        return 0;
    }
    do
    {
        int is_directory;
        if (strcmp(data.cFileName, ".") == 0 ||
            strcmp(data.cFileName, "..") == 0)
            continue;
        is_directory =
            (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!tver_add_entry(panel, data.cFileName, is_directory))
        {
            FindClose(handle);
            return 0;
        }
    } while (FindNextFileA(handle, &data));
    FindClose(handle);
    return 1;
#else
    DIR *directory;
    struct dirent *item;

    directory = opendir(panel->path);
    if (directory == NULL)
        return 0;
    if (!tver_is_root(panel->path) &&
        !tver_add_entry(panel, "..", 1))
    {
        closedir(directory);
        return 0;
    }
    while ((item = readdir(directory)) != NULL)
    {
        struct stat info;
        char full_path[PATH_MAX];
        int length;

        if (strcmp(item->d_name, ".") == 0 ||
            strcmp(item->d_name, "..") == 0)
            continue;
        length = snprintf(full_path, sizeof(full_path), "%s%s%s",
                          panel->path,
                          panel->path[strlen(panel->path) - 1] == '/'
                              ? "" : "/",
                          item->d_name);
        if (length < 0 || (size_t)length >= sizeof(full_path) ||
            stat(full_path, &info) != 0)
            continue;
        if (!tver_add_entry(panel, item->d_name, S_ISDIR(info.st_mode)))
        {
            closedir(directory);
            return 0;
        }
    }
    if (closedir(directory) != 0)
        return 0;
    return 1;
#endif
}

static int tver_append(
    char **buffer,
    size_t *length,
    size_t *capacity,
    char character
)
{
    char *grown;
    size_t next_capacity;

    if (*length + 1 >= *capacity)
    {
        next_capacity = *capacity == 0 ? 128 : *capacity * 2;
        if (next_capacity <= *capacity)
            return 0;
        grown = realloc(*buffer, next_capacity);
        if (grown == NULL)
            return 0;
        *buffer = grown;
        *capacity = next_capacity;
    }
    (*buffer)[(*length)++] = character;
    (*buffer)[*length] = '\0';
    return 1;
}

static int tver_refresh_panel(
    ussr_sched_program_t *program,
    tver_panel_t *panel,
    long keep_index
)
{
    size_t i;
    tver_entry_t *previous_entries = panel->entries;
    size_t previous_count = panel->entry_count;
    char *items = NULL;
    size_t length = 0;
    size_t capacity = 0;
    int ok = 0;

    panel->entries = NULL;
    panel->entry_count = 0;
    if (!tver_list_directory(panel))
        goto done;
    for (i = 0; i < panel->entry_count; ++i)
    {
        size_t old_index;
        for (old_index = 0; old_index < previous_count; ++old_index)
        {
            if (strcmp(panel->entries[i].name,
                       previous_entries[old_index].name) == 0)
            {
                panel->entries[i].marked =
                    previous_entries[old_index].marked;
                break;
            }
        }
    }
    if (panel->entry_count > 1)
        qsort(panel->entries, panel->entry_count,
              sizeof(*panel->entries), tver_entry_compare);

    for (i = 0; i < panel->entry_count; ++i)
    {
        const char *name = panel->entries[i].name;
        const unsigned char *cursor;
        if (i > 0 && !tver_append(&items, &length, &capacity, '|'))
            goto done;
        if (panel->entries[i].marked)
        {
            if (!tver_append(&items, &length, &capacity, '*') ||
                !tver_append(&items, &length, &capacity, ' '))
                goto done;
        }
        for (cursor = (const unsigned char *)name; *cursor != '\0'; ++cursor)
        {
            if ((*cursor == '\\' || *cursor == '|') &&
                !tver_append(&items, &length, &capacity, '\\'))
                goto done;
            if (!tver_append(&items, &length, &capacity, (char)*cursor))
                goto done;
        }
        if (panel->entries[i].directory &&
            strcmp(name, "..") != 0 &&
            !tver_append(&items, &length, &capacity, '/'))
            goto done;
    }
    if (items == NULL)
    {
        items = tver_strdup("");
        if (items == NULL)
            goto done;
    }
    if (!tver_set_widget_text(program, panel->list_widget, items))
        goto done;
    if (panel->entry_count > 0)
    {
        if (keep_index < 0)
            keep_index = 0;
        if ((size_t)keep_index >= panel->entry_count)
            keep_index = (long)panel->entry_count - 1;
        if (!tver_set_widget_index(program, panel->list_widget, keep_index))
            goto done;
    }
    ok = 1;
done:
    for (i = 0; i < previous_count; ++i)
        free(previous_entries[i].name);
    free(previous_entries);
    free(items);
    return ok;
}

static int tver_initialize(
    ussr_sched_program_t *program,
    const ussr_value_t *values,
    size_t count
)
{
    long handles[6];
    size_t i;

    if (!tver_error_arity("tver_initialize", count, 6))
        return 0;
    for (i = 0; i < 6; ++i)
        if (!tver_expect_integer(values, count, i, &handles[i]))
        {
            fprintf(stderr, "USSR: tver_initialize expects six widget handles\n");
            return 0;
        }
    ussr_tver_cleanup();
    tver_state.window = handles[0];
    tver_state.panels[0].path_widget = handles[1];
    tver_state.panels[0].list_widget = handles[2];
    tver_state.panels[1].path_widget = handles[3];
    tver_state.panels[1].list_widget = handles[4];
    tver_state.status = handles[5];
    tver_state.active = 0;
#ifdef _WIN32
    {
        DWORD path_length = GetCurrentDirectoryA(
            (DWORD)sizeof(tver_state.panels[0].path),
            tver_state.panels[0].path
        );
        if (path_length == 0 ||
            path_length >= sizeof(tver_state.panels[0].path))
        {
            fprintf(stderr, "USSR: Tver cannot read the current directory\n");
            return 0;
        }
    }
#else
    if (getcwd(tver_state.panels[0].path,
               sizeof(tver_state.panels[0].path)) == NULL)
    {
        fprintf(stderr, "USSR: Tver cannot read the current directory: %s\n",
                strerror(errno));
        return 0;
    }
#endif
    memcpy(tver_state.panels[1].path, tver_state.panels[0].path,
           strlen(tver_state.panels[0].path) + 1);
    for (i = 0; i < 2; ++i)
    {
        if (!tver_set_widget_text(program, tver_state.panels[i].path_widget,
                                  tver_state.panels[i].path) ||
            !tver_refresh_panel(program, &tver_state.panels[i], 0))
        {
            fprintf(stderr, "USSR: Tver could not read a panel directory\n");
            return 0;
        }
    }
    tver_set_status(program, "LEFT PANEL | TAB switches | INS marks | F6 moves | F8 deletes");
    return 1;
}

static int tver_selected_entry(
    ussr_sched_program_t *program,
    tver_panel_t *panel,
    long *index
)
{
    if (!tver_get_widget_index(program, panel->list_widget, index))
        return 0;
    return *index >= 0 && (size_t)*index < panel->entry_count;
}

static int tver_make_path(
    char *destination,
    size_t size,
    const char *directory,
    const char *name
)
{
    size_t directory_length = strlen(directory);
    size_t name_length = strlen(name);
    int separator = directory_length > 0 &&
                    directory[directory_length - 1] != '/' &&
                    directory[directory_length - 1] != '\\';

    if (directory_length + (size_t)separator + name_length >= size)
        return 0;
    memcpy(destination, directory, directory_length);
    if (separator)
        destination[directory_length++] =
#ifdef _WIN32
            '\\';
#else
            '/';
#endif
    memcpy(destination + directory_length, name, name_length + 1);
    return 1;
}

static int tver_parent_path(char *path)
{
#ifdef _WIN32
    size_t length = strlen(path);
    char *separator;
    if (tver_is_root(path))
        return 0;
    while (length > 0 && (path[length - 1] == '\\' || path[length - 1] == '/'))
        path[--length] = '\0';
    separator = strrchr(path, '\\');
    {
        char *slash = strrchr(path, '/');
        if (slash != NULL && (separator == NULL || slash > separator))
            separator = slash;
    }
    if (separator == NULL)
        return 0;
    if (separator == path + 2 && path[1] == ':')
        separator[1] = '\0';
    else if (separator == path)
        separator[1] = '\0';
    else
        *separator = '\0';
    return 1;
#else
    size_t length = strlen(path);
    char *separator;
    if (strcmp(path, "/") == 0)
        return 0;
    while (length > 1 && path[length - 1] == '/')
        path[--length] = '\0';
    separator = strrchr(path, '/');
    if (separator == NULL)
        return 0;
    if (separator == path)
        path[1] = '\0';
    else
        *separator = '\0';
    return 1;
#endif
}

static int tver_destination_exists(const char *path)
{
#ifdef _WIN32
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat info;
    return lstat(path, &info) == 0;
#endif
}

static const char *tver_delete_one(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return "Cannot find selected file";
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return "F8 deletes files only; select a file";
    if (!DeleteFileA(path))
        return "Delete failed; check permissions or file locks";
#else
    struct stat info;
    if (lstat(path, &info) != 0)
        return "Cannot find selected file";
    if (S_ISDIR(info.st_mode))
        return "F8 deletes files only; select a file";
    if (unlink(path) != 0)
        return "Delete failed; check permissions or file locks";
#endif
    return NULL;
}

static const char *tver_move_one(
    const char *source,
    const char *destination,
    int is_directory
)
{
    if (tver_destination_exists(destination))
        return "Move refused: destination already exists";
    if (is_directory)
        return "F6 moves files only; select a file";
#ifdef _WIN32
    if (!MoveFileA(source, destination))
        return "Move failed; check permissions or volume";
#else
    if (link(source, destination) != 0)
        return errno == EXDEV
                   ? "Move failed: source and destination are on different volumes"
                   : "Move failed; check permissions or file type";
    if (unlink(source) != 0)
    {
        (void)unlink(destination);
        return "Move could not remove the source after copying";
    }
#endif
    return NULL;
}

static size_t tver_marked_count(const tver_panel_t *panel)
{
    size_t i;
    size_t count = 0;
    for (i = 0; i < panel->entry_count; ++i)
        if (panel->entries[i].marked)
            ++count;
    return count;
}

static void tver_handle_key(
    ussr_sched_program_t *program,
    const ussr_value_t *values,
    size_t count
)
{
    long window;
    long key;
    long selected;
    tver_panel_t *panel;
    char message[512];

    if (!tver_error_arity("tver_handle_key", count, 2) ||
        !tver_expect_integer(values, count, 0, &window) ||
        !tver_expect_integer(values, count, 1, &key) ||
        window != tver_state.window)
    {
        fprintf(stderr, "USSR: tver_handle_key received invalid arguments\n");
        return;
    }

    if (key == 258)
    {
        tver_state.active = 1 - tver_state.active;
        if (!tver_set_focus(program,
                            tver_state.panels[tver_state.active].list_widget))
        {
            tver_set_status(program, "Could not focus the active panel");
            return;
        }
        tver_state.pending_delete[0] = '\0';
        tver_set_status(program, tver_state.active == 0
            ? "LEFT PANEL | TAB switches | INS marks | F6 moves | F8 deletes"
            : "RIGHT PANEL | TAB switches | INS marks | F6 moves | F8 deletes");
        return;
    }

    panel = &tver_state.panels[tver_state.active];
    if (!tver_selected_entry(program, panel, &selected))
    {
        tver_set_status(program, "No file is selected");
        return;
    }

    if (key == 260)
    {
        if (strcmp(panel->entries[selected].name, "..") == 0)
            return;
        if (panel->entries[selected].directory)
        {
            tver_set_status(program,
                            "Directories cannot be marked in this version");
            return;
        }
        panel->entries[selected].marked = !panel->entries[selected].marked;
        if (!tver_refresh_panel(program, panel,
                                selected + 1 < (long)panel->entry_count
                                    ? selected + 1 : selected))
        {
            tver_set_status(program, "Could not refresh marked files");
            return;
        }
        snprintf(message, sizeof(message), "%lu file(s) marked in this panel",
                 (unsigned long)tver_marked_count(panel));
        tver_set_status(program, message);
        return;
    }

    if (key == 257 || key == 335)
    {
        tver_entry_t *entry = &panel->entries[selected];
        char next_path[PATH_MAX];
        int path_ok;

        if (!entry->directory)
            return;
        if (strcmp(entry->name, "..") == 0)
        {
            memcpy(next_path, panel->path, strlen(panel->path) + 1);
            path_ok = tver_parent_path(next_path);
        }
        else
            path_ok = tver_make_path(next_path, sizeof(next_path),
                                     panel->path, entry->name);
        if (!path_ok)
        {
            tver_set_status(program, "Cannot navigate above this directory");
            return;
        }
        if (strlen(next_path) >= sizeof(panel->path))
        {
            tver_set_status(program, "Directory path is too long");
            return;
        }
        {
            size_t mark_index;
            for (mark_index = 0; mark_index < panel->entry_count; ++mark_index)
                panel->entries[mark_index].marked = 0;
        }
        memcpy(panel->path, next_path, strlen(next_path) + 1);
        tver_state.pending_delete[0] = '\0';
        if (!tver_set_widget_text(program, panel->path_widget, panel->path) ||
            !tver_refresh_panel(program, panel, 0))
        {
            tver_set_status(program, "Could not open selected directory");
            return;
        }
        return;
    }

    if (key == 295)
    {
        tver_panel_t *destination = &tver_state.panels[1 - tver_state.active];
        size_t i;
        size_t marked = tver_marked_count(panel);
        size_t moved = 0;
        const char *failure = NULL;

        tver_state.pending_delete[0] = '\0';
        if (marked == 0 &&
            strcmp(panel->entries[selected].name, "..") == 0)
        {
            tver_set_status(program, "Select a file to move");
            return;
        }
        for (i = 0; i < panel->entry_count; ++i)
        {
            char source_path[PATH_MAX];
            char destination_path[PATH_MAX];
            tver_entry_t *entry = &panel->entries[i];
            if ((marked > 0 && !entry->marked) ||
                (marked == 0 && (long)i != selected))
                continue;
            if (strcmp(entry->name, "..") == 0)
                continue;
            if (!tver_make_path(source_path, sizeof(source_path),
                                panel->path, entry->name) ||
                !tver_make_path(destination_path, sizeof(destination_path),
                                destination->path, entry->name))
            {
                failure = "Move failed: path is too long";
                break;
            }
            failure = tver_move_one(source_path, destination_path,
                                    entry->directory);
            if (failure != NULL)
                break;
            ++moved;
            entry->marked = 0;
        }
        if (moved > 0)
        {
            if (!tver_refresh_panel(program, panel, selected) ||
                !tver_refresh_panel(program, destination, 0))
            {
                tver_set_status(program, "Moved files, but refreshing a panel failed");
                return;
            }
        }
        if (failure != NULL && moved > 0)
            snprintf(message, sizeof(message), "%lu moved; %s",
                     (unsigned long)moved, failure);
        else if (failure != NULL)
            snprintf(message, sizeof(message), "%s", failure);
        else
            snprintf(message, sizeof(message), "%lu file(s) moved",
                     (unsigned long)moved);
        tver_set_status(program, message);
        return;
    }

    if (key == 297)
    {
        size_t marked = tver_marked_count(panel);
        size_t i;
        size_t deleted = 0;
        const char *failure = NULL;
        char confirmation[PATH_MAX * 2 + 128];

        if (marked == 0 &&
            strcmp(panel->entries[selected].name, "..") == 0)
        {
            tver_set_status(program, "Select a file to delete");
            return;
        }
        snprintf(confirmation, sizeof(confirmation),
                 "%d|%s|%ld|%lu", tver_state.active, panel->path, selected,
                 (unsigned long)marked);
        if (strcmp(tver_state.pending_delete, confirmation) != 0)
        {
            memcpy(tver_state.pending_delete, confirmation,
                   strlen(confirmation) + 1);
            if (marked > 0)
                snprintf(message, sizeof(message),
                         "Press F8 again to delete %lu marked file(s)",
                         (unsigned long)marked);
            else
                snprintf(message, sizeof(message),
                         "Press F8 again to delete %s",
                         panel->entries[selected].name);
            tver_set_status(program, message);
            return;
        }
        tver_state.pending_delete[0] = '\0';
        for (i = 0; i < panel->entry_count; ++i)
        {
            char path[PATH_MAX];
            tver_entry_t *entry = &panel->entries[i];
            if ((marked > 0 && !entry->marked) ||
                (marked == 0 && (long)i != selected))
                continue;
            if (strcmp(entry->name, "..") == 0)
                continue;
            if (!tver_make_path(path, sizeof(path), panel->path, entry->name))
            {
                failure = "Delete failed: path is too long";
                break;
            }
            failure = tver_delete_one(path);
            if (failure != NULL)
                break;
            ++deleted;
            entry->marked = 0;
        }
        if (deleted > 0 && !tver_refresh_panel(program, panel, selected))
        {
            tver_set_status(program, "Deleted files, but refreshing the panel failed");
            return;
        }
        if (failure != NULL && deleted > 0)
            snprintf(message, sizeof(message), "%lu deleted; %s",
                     (unsigned long)deleted, failure);
        else if (failure != NULL)
            snprintf(message, sizeof(message), "%s", failure);
        else
            snprintf(message, sizeof(message), "%lu file(s) deleted",
                     (unsigned long)deleted);
        tver_set_status(program, message);
        return;
    }
}

static void tver_activate_panel(
    ussr_sched_program_t *program,
    const ussr_value_t *values,
    size_t count
)
{
    long widget;
    if (!tver_error_arity("tver_activate_panel", count, 1) ||
        !tver_expect_integer(values, count, 0, &widget))
    {
        fprintf(stderr, "USSR: tver_activate_panel expects a widget handle\n");
        return;
    }
    if (widget == tver_state.panels[0].list_widget)
        tver_state.active = 0;
    else if (widget == tver_state.panels[1].list_widget)
        tver_state.active = 1;
    else
        return;
    tver_state.pending_delete[0] = '\0';
    tver_set_status(program, tver_state.active == 0
        ? "LEFT PANEL | TAB switches | INS marks | F6 moves | F8 deletes"
        : "RIGHT PANEL | TAB switches | INS marks | F6 moves | F8 deletes");
}

int ussr_tver_dispatch(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
)
{
    if (name == NULL || strncmp(name, "tver_", 5) != 0)
        return 0;
    if (strcmp(name, "tver_initialize") == 0)
    {
        if (!tver_initialize(program, values, value_count))
            return -1;
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "tver_handle_key") == 0)
    {
        long window;
        long key;
        if (value_count != 2 ||
            !tver_expect_integer(values, value_count, 0, &window) ||
            !tver_expect_integer(values, value_count, 1, &key) ||
            window != tver_state.window)
        {
            fprintf(stderr, "USSR: tver_handle_key expects this window and a key code\n");
            return -1;
        }
        (void)key;
        tver_handle_key(program, values, value_count);
        *out = ussr_integer(0);
        return 1;
    }
    if (strcmp(name, "tver_activate_panel") == 0)
    {
        long widget;
        if (value_count != 1 ||
            !tver_expect_integer(values, value_count, 0, &widget) ||
            (widget != tver_state.panels[0].list_widget &&
             widget != tver_state.panels[1].list_widget))
        {
            fprintf(stderr,
                    "USSR: tver_activate_panel expects one of this app's lists\n");
            return -1;
        }
        tver_activate_panel(program, values, value_count);
        *out = ussr_integer(0);
        return 1;
    }
    fprintf(stderr, "USSR: unknown Tver native command '%s'\n", name);
    return -1;
}

void ussr_tver_cleanup(void)
{
    size_t panel_index;
    for (panel_index = 0; panel_index < 2; ++panel_index)
    {
        tver_panel_t *panel = &tver_state.panels[panel_index];
        size_t i;
        for (i = 0; i < panel->entry_count; ++i)
            free(panel->entries[i].name);
        free(panel->entries);
    }
    memset(&tver_state, 0, sizeof(tver_state));
}
