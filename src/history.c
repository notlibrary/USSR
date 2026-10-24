#include "history.h"

#include "ussr_oop_builtins.h" /* ussr_env_get */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HISTORY_DEFAULT_SIZE 1000

static char *g_history_path = NULL;
static char *g_history_last = NULL;

static char *history_strdup(const char *text)
{
    char *copy = malloc(strlen(text) + 1);

    if (copy != NULL)
        strcpy(copy, text);
    return copy;
}

char *ussr_home_file(const char *name)
{
    char *home = ussr_env_get("HOME");
    char *path = NULL;

#ifdef _WIN32
    if (home == NULL || home[0] == '\0')
    {
        free(home);
        home = ussr_env_get("USERPROFILE");
    }
#endif
    if (home != NULL && home[0] != '\0')
    {
        size_t length = strlen(home);
        int slash = home[length - 1] == '/' || home[length - 1] == '\\';

        path = malloc(length + 1 + strlen(name) + 1);
        if (path != NULL)
            sprintf(path, "%s%s%s", home, slash ? "" : "/", name);
    }
    free(home);
    return path;
}

static int history_size(void)
{
    char *text = ussr_env_get("USSR_HISTSIZE");
    int size = HISTORY_DEFAULT_SIZE;

    if (text != NULL && text[0] != '\0')
    {
        char *end;
        long value = strtol(text, &end, 10);

        if (*end == '\0' && value >= 0 && value <= 1000000)
            size = (int)value;
    }
    free(text);
    return size;
}

/* One line without its line ending, malloc'd; NULL at end of file. */
static char *history_read_line(FILE *file)
{
    size_t length = 0;
    size_t capacity = 128;
    char *line = malloc(capacity);
    int c;

    if (line == NULL)
        return NULL;
    while ((c = fgetc(file)) != EOF && c != '\n')
    {
        if (length + 1 >= capacity)
        {
            char *grown = realloc(line, capacity * 2);

            if (grown == NULL)
            {
                free(line);
                return NULL;
            }
            line = grown;
            capacity *= 2;
        }
        line[length++] = (char)c;
    }
    if (c == EOF && length == 0)
    {
        free(line);
        return NULL;
    }
    if (length > 0 && line[length - 1] == '\r')
        --length;
    line[length] = '\0';
    return line;
}

int ussr_history_load(void (*add)(const char *line))
{
    char *chosen = ussr_env_get("USSR_HISTFILE");
    char **lines = NULL;
    size_t count = 0;
    size_t capacity = 0;
    size_t first;
    size_t i;
    int size = history_size();
    FILE *file;

    free(g_history_path);
    g_history_path = NULL;
    if (chosen != NULL)
    {
        if (chosen[0] == '\0' || size == 0)
        {
            free(chosen);
            return 0; /* no history file */
        }
        g_history_path = chosen;
    }
    else if (size > 0)
        g_history_path = ussr_home_file(".ussr_history");
    if (g_history_path == NULL)
        return 0;

    file = fopen(g_history_path, "rb");
    if (file == NULL)
        return 0;
    for (;;)
    {
        char *line = history_read_line(file);

        if (line == NULL)
            break;
        if (line[0] == '\0')
        {
            free(line);
            continue;
        }
        if (count == capacity)
        {
            size_t grown_capacity = capacity ? capacity * 2 : 256;
            char **grown = realloc(lines, grown_capacity * sizeof(*grown));

            if (grown == NULL)
            {
                free(line);
                break;
            }
            lines = grown;
            capacity = grown_capacity;
        }
        lines[count++] = line;
    }
    fclose(file);

    first = count > (size_t)size ? count - (size_t)size : 0;
    for (i = first; i < count; ++i)
        add(lines[i]);
    if (count > first)
    {
        free(g_history_last);
        g_history_last = history_strdup(lines[count - 1]);
    }

    /* keep the file at USSR_HISTSIZE entries */
    if (first > 0)
    {
        file = fopen(g_history_path, "wb");
        if (file != NULL)
        {
            for (i = first; i < count; ++i)
                fprintf(file, "%s\n", lines[i]);
            fclose(file);
        }
    }

    for (i = 0; i < count; ++i)
        free(lines[i]);
    free(lines);
    return (int)(count - first);
}

void ussr_history_append(const char *line)
{
    FILE *file;

    if (g_history_path == NULL || line == NULL || line[0] == '\0' ||
        strchr(line, '\n') != NULL || strchr(line, '\r') != NULL)
        return;
    if (g_history_last != NULL && strcmp(g_history_last, line) == 0)
        return;
    file = fopen(g_history_path, "ab");
    if (file == NULL)
        return;
    fprintf(file, "%s\n", line);
    fclose(file);
    free(g_history_last);
    g_history_last = history_strdup(line);
}
