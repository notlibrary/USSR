#include "alias.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *name;
    char **words;
    size_t count;
} alias_entry_t;

static alias_entry_t *g_aliases = NULL;
static size_t g_alias_count = 0;
static size_t g_alias_capacity = 0;

/* How many aliases one command may go through: ll -> l -> ls ... */
#define ALIAS_MAX_DEPTH 16

static char *alias_strdup(const char *text)
{
    char *copy = malloc(strlen(text) + 1);

    if (copy != NULL)
        strcpy(copy, text);
    return copy;
}

static void alias_free_words(char **words, size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i)
        free(words[i]);
    free(words);
}

/* A command name the lexer accepts: [A-Za-z_][A-Za-z0-9_]* */
static int alias_name_valid(const char *name)
{
    const char *p;

    if (name == NULL || !(isalpha((unsigned char)name[0]) || name[0] == '_'))
        return 0;
    for (p = name + 1; *p != '\0'; ++p)
    {
        if (!isalnum((unsigned char)*p) && *p != '_')
            return 0;
    }
    return 1;
}

/* Words the compiler treats as syntax, not as commands. */
static int alias_name_reserved(const char *name)
{
    static const char *const reserved[] = {
        "if", "elif", "else", "while", "do", "loop", "break", "continue",
        "return", "global", "choose", "option", "alias", NULL
    };
    size_t i;

    for (i = 0; reserved[i] != NULL; ++i)
    {
        if (strcmp(reserved[i], name) == 0)
            return 1;
    }
    return 0;
}

int ussr_alias_set(const char *name, const char *const *words, size_t count,
                   char *error, size_t error_size)
{
    char **copy = NULL;
    size_t i;

    if (!alias_name_valid(name))
    {
        snprintf(error, error_size,
                 "'%s' is not a command name (letters, digits, _)",
                 name != NULL ? name : "");
        return -1;
    }
    if (alias_name_reserved(name))
    {
        snprintf(error, error_size, "'%s' cannot be an alias", name);
        return -1;
    }
    if (count > 0)
    {
        /* The first word becomes a command name; a path is allowed
         * (only ./ll style call sites are never expanded). */
        if (words[0][0] == '\0' || strpbrk(words[0], " \t\r\n") != NULL)
        {
            snprintf(error, error_size,
                     "'%s' is not a command to run", words[0]);
            return -1;
        }
        copy = calloc(count, sizeof(*copy));
        if (copy == NULL)
        {
            snprintf(error, error_size, "out of memory");
            return -1;
        }
        for (i = 0; i < count; ++i)
        {
            copy[i] = alias_strdup(words[i]);
            if (copy[i] == NULL)
            {
                alias_free_words(copy, i);
                snprintf(error, error_size, "out of memory");
                return -1;
            }
        }
    }

    for (i = 0; i < g_alias_count; ++i)
    {
        if (strcmp(g_aliases[i].name, name) != 0)
            continue;
        alias_free_words(g_aliases[i].words, g_aliases[i].count);
        if (count > 0)
        {
            g_aliases[i].words = copy;
            g_aliases[i].count = count;
        }
        else
        {
            free(g_aliases[i].name);
            memmove(&g_aliases[i], &g_aliases[i + 1],
                    (g_alias_count - i - 1) * sizeof(*g_aliases));
            --g_alias_count;
        }
        return 0;
    }

    if (count == 0)
        return 0; /* removing an alias that does not exist */

    if (g_alias_count == g_alias_capacity)
    {
        size_t capacity = g_alias_capacity ? g_alias_capacity * 2 : 8;
        alias_entry_t *grown = realloc(g_aliases, capacity * sizeof(*grown));

        if (grown == NULL)
        {
            alias_free_words(copy, count);
            snprintf(error, error_size, "out of memory");
            return -1;
        }
        g_aliases = grown;
        g_alias_capacity = capacity;
    }
    g_aliases[g_alias_count].name = alias_strdup(name);
    if (g_aliases[g_alias_count].name == NULL)
    {
        alias_free_words(copy, count);
        snprintf(error, error_size, "out of memory");
        return -1;
    }
    g_aliases[g_alias_count].words = copy;
    g_aliases[g_alias_count].count = count;
    ++g_alias_count;
    return 0;
}

const char *const *ussr_alias_find(const char *name, size_t *count)
{
    size_t i;

    for (i = 0; i < g_alias_count; ++i)
    {
        if (strcmp(g_aliases[i].name, name) == 0)
        {
            *count = g_aliases[i].count;
            return (const char *const *)g_aliases[i].words;
        }
    }
    return NULL;
}

const char *ussr_alias_at(size_t index, const char *const **words,
                          size_t *count)
{
    if (index >= g_alias_count)
        return NULL;
    *words = (const char *const *)g_aliases[index].words;
    *count = g_aliases[index].count;
    return g_aliases[index].name;
}

/* ------------------------------------------------------------- */
/* expansion                                                        */
/* ------------------------------------------------------------- */

/* Commands whose last argument is a block define a function or are
 * control flow; their names are never replaced. */
static int alias_takes_block(const ussr_command_t *command)
{
    return command->argument_count > 0 &&
           command->arguments[command->argument_count - 1].type ==
               USSR_ARGUMENT_COMMAND_LIST;
}

/* Put words[1..count) in front of the command's arguments and make
 * words[0] its name. */
static int alias_apply(ussr_command_t *command,
                       const char *const *words, size_t count)
{
    size_t extra = count - 1;
    ussr_argument_t *arguments;
    char *name;
    size_t i;

    name = alias_strdup(words[0]);
    if (name == NULL)
        return -1;

    if (extra > 0)
    {
        arguments = calloc(command->argument_count + extra,
                           sizeof(*arguments));
        if (arguments == NULL)
        {
            free(name);
            return -1;
        }
        for (i = 0; i < extra; ++i)
        {
            arguments[i].type = USSR_ARGUMENT_VALUE;
            arguments[i].assignment = 0;
            arguments[i].data.value = ussr_string(words[i + 1]);
            if (arguments[i].data.value.data.string == NULL)
            {
                while (i-- > 0)
                    ussr_value_free(&arguments[i].data.value);
                free(arguments);
                free(name);
                return -1;
            }
        }
        if (command->argument_count > 0)
            memcpy(arguments + extra, command->arguments,
                   command->argument_count * sizeof(*arguments));
        free(command->arguments);
        command->arguments = arguments;
        command->argument_count += extra;
    }

    free(command->name);
    command->name = name;
    return 0;
}

static int alias_expand_command(ussr_command_t *command)
{
    const char *seen[ALIAS_MAX_DEPTH];
    size_t depth = 0;
    size_t i;

    for (i = 0; i < command->argument_count; ++i)
    {
        if (command->arguments[i].type == USSR_ARGUMENT_COMMAND_LIST &&
            ussr_alias_expand(command->arguments[i].data.command_list) != 0)
            return -1;
    }

    if (command->path != NULL || command->name == NULL ||
        alias_takes_block(command))
        return 0;

    while (depth < ALIAS_MAX_DEPTH)
    {
        const char *const *words;
        size_t count;
        size_t j;
        int repeated = 0;

        words = ussr_alias_find(command->name, &count);
        if (words == NULL)
            break;
        for (j = 0; j < depth; ++j)
        {
            if (strcmp(seen[j], command->name) == 0)
                repeated = 1;
        }
        if (repeated)
            break;
        /* seen[] points into the alias table, which lives on */
        for (j = 0; j < g_alias_count; ++j)
        {
            if (strcmp(g_aliases[j].name, command->name) == 0)
                seen[depth] = g_aliases[j].name;
        }
        ++depth;
        if (alias_apply(command, words, count) != 0)
            return -1;
    }
    return 0;
}

int ussr_alias_expand(ussr_command_list_t *list)
{
    ussr_command_t *command;

    if (list == NULL || g_alias_count == 0)
        return 0;
    for (command = list->head; command != NULL; command = command->next)
    {
        if (alias_expand_command(command) != 0)
        {
            fprintf(stderr, "USSR: out of memory expanding aliases\n");
            return -1;
        }
    }
    return 0;
}
