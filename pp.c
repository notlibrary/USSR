#include "pp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <limits.h>
#include <stdint.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char *pp_strdup(const char *src)
{
    size_t length;
    char *copy;

    if (src == NULL)
        return NULL;

    length = strlen(src);

    copy = malloc(length + 1);
    if (copy == NULL)
        return NULL;

    memcpy(copy, src, length + 1);

    return copy;
}

static char *pp_strndup(const char *src, size_t length)
{
    char *copy;

    copy = malloc(length + 1);
    if (copy == NULL)
        return NULL;

    memcpy(copy, src, length);
    copy[length] = '\0';

    return copy;
}

static const char *pp_skip_space(const char *text)
{
    while (*text != '\0' && isspace((unsigned char)*text))
        ++text;

    return text;
}

static int pp_append(
    ussr_preprocessor_t *pp,
    const char *text
)
{
    size_t length;
    size_t required;
    size_t new_capacity;
    char *new_output;

    if (pp == NULL || text == NULL)
        return -1;

    length = strlen(text);
    required = pp->output_length + length + 1;

    if (required <= pp->output_capacity)
    {
        memcpy(
            pp->output + pp->output_length,
            text,
            length + 1
        );

        pp->output_length += length;

        return 0;
    }

    new_capacity = pp->output_capacity;

    if (new_capacity == 0)
        new_capacity = 1024;

    while (new_capacity < required)
    {
        if (new_capacity > SIZE_MAX / 2)
        {
            new_capacity = required;
            break;
        }

        new_capacity *= 2;
    }

    new_output = realloc(pp->output, new_capacity);

    if (new_output == NULL)
        return -1;

    pp->output = new_output;
    pp->output_capacity = new_capacity;

    memcpy(
        pp->output + pp->output_length,
        text,
        length + 1
    );

    pp->output_length += length;

    return 0;
}

static ussr_pp_define_t *pp_find_define(
    ussr_preprocessor_t *pp,
    const char *name
)
{
    ussr_pp_define_t *define;

    for (define = pp->defines;
         define != NULL;
         define = define->next)
    {
        if (strcmp(define->name, name) == 0)
            return define;
    }

    return NULL;
}

static int pp_define(
    ussr_preprocessor_t *pp,
    const char *name,
    const char *value
)
{
    ussr_pp_define_t *define;
    char *new_value;

    define = pp_find_define(pp, name);

    if (define != NULL)
    {
        new_value = pp_strdup(value);

        if (new_value == NULL)
            return -1;

        free(define->value);
        define->value = new_value;

        return 0;
    }

    define = calloc(1, sizeof(*define));

    if (define == NULL)
        return -1;

    define->name = pp_strdup(name);
    define->value = pp_strdup(value);

    if (define->name == NULL || define->value == NULL)
    {
        free(define->name);
        free(define->value);
        free(define);

        return -1;
    }

    define->next = pp->defines;
    pp->defines = define;

    return 0;
}

static int pp_is_defined(
    ussr_preprocessor_t *pp,
    const char *name
)
{
    return pp_find_define(pp, name) != NULL;
}

static int pp_push_include(
    ussr_preprocessor_t *pp,
    const char *filename
)
{
    size_t i;

    if (pp->include_depth >= USSR_PP_MAX_INCLUDE_DEPTH)
    {
        fprintf(
            stderr,
            "USSR: maximum include depth exceeded: %s\n",
            filename
        );

        return -1;
    }

    for (i = 0; i < pp->include_depth; ++i)
    {
        if (strcmp(pp->include_stack[i], filename) == 0)
        {
            fprintf(
                stderr,
                "USSR: recursive include detected: %s\n",
                filename
            );

            return -1;
        }
    }

    if (pp->include_depth >= pp->include_capacity)
    {
        size_t new_capacity;
        char **new_stack;

        new_capacity =
            pp->include_capacity == 0
                ? 8
                : pp->include_capacity * 2;

        new_stack = realloc(
            pp->include_stack,
            new_capacity * sizeof(*new_stack)
        );

        if (new_stack == NULL)
            return -1;

        pp->include_stack = new_stack;
        pp->include_capacity = new_capacity;
    }

    pp->include_stack[pp->include_depth] =
        pp_strdup(filename);

    if (pp->include_stack[pp->include_depth] == NULL)
        return -1;

    ++pp->include_depth;

    return 0;
}

static void pp_pop_include(
    ussr_preprocessor_t *pp
)
{
    if (pp->include_depth == 0)
        return;

    --pp->include_depth;

    free(pp->include_stack[pp->include_depth]);
    pp->include_stack[pp->include_depth] = NULL;
}

static int pp_resolve_include_path(
    const char *filename,
    const char *current_file,
    char *result,
    size_t result_size
)
{
    const char *slash;
    size_t directory_length;

    if (filename == NULL ||
        current_file == NULL ||
        result == NULL ||
        result_size == 0)
    {
        return -1;
    }

    /*
     * Absolute Unix path.
     */
    if (filename[0] == '/')
    {
        if (snprintf(
                result,
                result_size,
                "%s",
                filename
            ) >= (int)result_size)
        {
            return -1;
        }

        return 0;
    }

    /*
     * Resolve relative includes against the directory
     * containing the current source file.
     *
     * Example:
     *
     *   current_file = tests/pp.su
     *   filename     = library.su
     *
     *   result       = tests/library.su
     */
    slash = strrchr(current_file, '/');

    if (slash == NULL)
    {
        if (snprintf(
                result,
                result_size,
                "%s",
                filename
            ) >= (int)result_size)
        {
            return -1;
        }

        return 0;
    }

    directory_length = (size_t)(slash - current_file);

    if (directory_length == 0)
    {
        if (snprintf(
                result,
                result_size,
                "/%s",
                filename
            ) >= (int)result_size)
        {
            return -1;
        }

        return 0;
    }

    if (directory_length + 1 + strlen(filename) + 1 > result_size)
    {
        return -1;
    }

    memcpy(result, current_file, directory_length);
    result[directory_length] = '/';

    strcpy(
        result + directory_length + 1,
        filename
    );

    return 0;
}

static int pp_process_include(
    ussr_preprocessor_t *pp,
    const char *filename
)
{
    char resolved[PATH_MAX];

const char *current_file = NULL;

if (pp->include_depth > 0)
{
    current_file = pp->include_stack[pp->include_depth - 1];
}

if (pp_resolve_include_path(
        filename,
        current_file,
        resolved,
        sizeof(resolved)
    ) != 0)
{
    return -1;
}

    return ussr_pp_process_file(pp, resolved);
}

static int pp_condition_push(
    ussr_preprocessor_t *pp,
    int condition
)
{
    int parent_active;

    if (pp->conditional_depth >= USSR_PP_MAX_INCLUDE_DEPTH)
    {
        fprintf(
            stderr,
            "USSR: conditional nesting too deep\n"
        );

        return -1;
    }

    parent_active = pp->current_active;

    pp->conditional_active[pp->conditional_depth] =
        parent_active && condition;

    pp->conditional_seen_else[pp->conditional_depth] = 0;

    ++pp->conditional_depth;

    pp->current_active =
        pp->conditional_active[
            pp->conditional_depth - 1
        ];

    return 0;
}

static int pp_condition_else(
    ussr_preprocessor_t *pp
)
{
    size_t index;
    int parent_active;

    if (pp->conditional_depth == 0)
    {
        fprintf(
            stderr,
            "USSR: unexpected $else\n"
        );

        return -1;
    }

    index = pp->conditional_depth - 1;

    if (pp->conditional_seen_else[index])
    {
        fprintf(
            stderr,
            "USSR: duplicate $else\n"
        );

        return -1;
    }

    pp->conditional_seen_else[index] = 1;

    if (index == 0)
        parent_active = 1;
    else
        parent_active =
            pp->conditional_active[index - 1];

    pp->conditional_active[index] =
        parent_active &&
        !pp->conditional_active[index];

    pp->current_active =
        pp->conditional_active[index];

    return 0;
}

static int pp_condition_pop(
    ussr_preprocessor_t *pp
)
{
    if (pp->conditional_depth == 0)
    {
        fprintf(
            stderr,
            "USSR: unexpected $endif\n"
        );

        return -1;
    }

    --pp->conditional_depth;

    if (pp->conditional_depth == 0)
        pp->current_active = 1;
    else
        pp->current_active =
            pp->conditional_active[
                pp->conditional_depth - 1
            ];

    return 0;
}

static int pp_parse_defined(
    ussr_preprocessor_t *pp,
    const char *text
)
{
    const char *cursor;
    const char *start;
    char *name;
    size_t length;
    int result;

    if (pp == NULL || text == NULL)
        return -1;

    cursor = pp_skip_space(text);

    if (strncmp(cursor, "defined", 7) != 0 ||
        (isalnum((unsigned char)cursor[7]) ||
         cursor[7] == '_'))
    {
        fprintf(
            stderr,
            "USSR: expected defined(...)\n"
        );

        return -1;
    }

    cursor += 7;
    cursor = pp_skip_space(cursor);

    if (*cursor != '(')
    {
        fprintf(
            stderr,
            "USSR: expected '(' after defined\n"
        );

        return -1;
    }

    ++cursor;
    cursor = pp_skip_space(cursor);

    start = cursor;

    if (!isalpha((unsigned char)*cursor) &&
        *cursor != '_')
    {
        fprintf(
            stderr,
            "USSR: expected identifier in defined(...)\n"
        );

        return -1;
    }

    ++cursor;

    while (*cursor != '\0' &&
           (isalnum((unsigned char)*cursor) ||
            *cursor == '_'))
    {
        ++cursor;
    }

    length = (size_t)(cursor - start);

    name = pp_strndup(start, length);

    if (name == NULL)
        return -1;

    cursor = pp_skip_space(cursor);

    if (*cursor != ')')
    {
        fprintf(
            stderr,
            "USSR: expected ')' after defined identifier\n"
        );

        free(name);
        return -1;
    }

    ++cursor;
    cursor = pp_skip_space(cursor);

    /*
     * Nothing except whitespace is allowed after
     * defined(NAME).
     */
    if (*cursor != '\0')
    {
        fprintf(
            stderr,
            "USSR: unexpected text after defined(...)\n"
        );

        free(name);
        return -1;
    }

    result = pp_is_defined(pp, name);

    free(name);

    return result;
}

static int pp_parse_define(
    ussr_preprocessor_t *pp,
    const char *text
)
{
    const char *cursor;
    const char *name_start;
    const char *value_start;
    char *name;
    char *value;
    size_t name_length;
    int result;

    cursor = pp_skip_space(text);

    name_start = cursor;

    while (*cursor != '\0' &&
           (isalnum((unsigned char)*cursor) ||
            *cursor == '_'))
    {
        ++cursor;
    }

    name_length = (size_t)(cursor - name_start);

    if (name_length == 0)
    {
        fprintf(
            stderr,
            "USSR: $define requires a name\n"
        );

        return -1;
    }

    name = pp_strndup(name_start, name_length);

    if (name == NULL)
        return -1;

    cursor = pp_skip_space(cursor);

    if (*cursor == '\0')
        value_start = "";
    else
        value_start = cursor;

    value = pp_strdup(value_start);

    if (value == NULL)
    {
        free(name);
        return -1;
    }

    result = pp_define(pp, name, value);

    free(name);
    free(value);

    return result;
}

static int pp_parse_include(
    ussr_preprocessor_t *pp,
    const char *text
)
{
    const char *cursor;
    const char *start;
    char *filename;
    size_t length;
    int result;

    cursor = pp_skip_space(text);

    if (*cursor == '"')
    {
        ++cursor;
        start = cursor;

        while (*cursor != '\0' && *cursor != '"')
            ++cursor;

        if (*cursor != '"')
        {
            fprintf(
                stderr,
                "USSR: unterminated $include filename\n"
            );

            return -1;
        }

        length = (size_t)(cursor - start);

        filename = pp_strndup(start, length);

        if (filename == NULL)
            return -1;

        result = pp_process_include(pp, filename);

        free(filename);

        return result;
    }

    start = cursor;

    while (*cursor != '\0' &&
           !isspace((unsigned char)*cursor))
    {
        ++cursor;
    }

    length = (size_t)(cursor - start);

    if (length == 0)
    {
        fprintf(
            stderr,
            "USSR: $include requires a filename\n"
        );

        return -1;
    }

    filename = pp_strndup(start, length);

    if (filename == NULL)
        return -1;

    result = pp_process_include(pp, filename);

    free(filename);

    return result;
}

static int pp_process_directive(
    ussr_preprocessor_t *pp,
    const char *line
)
{
    const char *cursor;
    const char *directive_start;
    char *directive;
    size_t directive_length;
    int condition;

    cursor = pp_skip_space(line);

    if (*cursor != '$')
        return 0;

    ++cursor;

    directive_start = cursor;

    while (*cursor != '\0' &&
           (isalnum((unsigned char)*cursor) ||
            *cursor == '_'))
    {
        ++cursor;
    }

    directive_length =
        (size_t)(cursor - directive_start);

    if (directive_length == 0)
    {
        fprintf(
            stderr,
            "USSR: invalid preprocessor directive\n"
        );

        return -1;
    }

    directive =
        pp_strndup(
            directive_start,
            directive_length
        );

    if (directive == NULL)
        return -1;

    cursor = pp_skip_space(cursor);

    if (strcmp(directive, "define") == 0)
    {
        int result;

        result = pp_parse_define(pp, cursor);

        free(directive);

        return result;
    }

    if (strcmp(directive, "include") == 0)
    {
        int result;

        if (!pp->current_active)
        {
            free(directive);
            return 0;
        }

        result = pp_parse_include(pp, cursor);

        free(directive);

        return result;
    }

    if (strcmp(directive, "ifdef") == 0)
    {
        const char *name_start;
        char *name;
        size_t name_length;
        int result;

        name_start = cursor;

        while (*cursor != '\0' &&
               (isalnum((unsigned char)*cursor) ||
                *cursor == '_'))
        {
            ++cursor;
        }

        name_length =
            (size_t)(cursor - name_start);

        if (name_length == 0)
        {
            fprintf(
                stderr,
                "USSR: $ifdef requires a name\n"
            );

            free(directive);
            return -1;
        }

        name = pp_strndup(name_start, name_length);

        if (name == NULL)
        {
            free(directive);
            return -1;
        }

        condition = pp_is_defined(pp, name);

        free(name);

        result = pp_condition_push(pp, condition);

        free(directive);

        return result;
    }

    if (strcmp(directive, "if") == 0)
    {
        int result;

        condition = pp_parse_defined(pp, cursor);

        if (condition < 0)
        {
            free(directive);
            return -1;
        }

        result = pp_condition_push(pp, condition);

        free(directive);

        return result;
    }

    if (strcmp(directive, "else") == 0)
    {
        int result;

        result = pp_condition_else(pp);

        free(directive);

        return result;
    }

    if (strcmp(directive, "endif") == 0)
    {
        int result;

        result = pp_condition_pop(pp);

        free(directive);

        return result;
    }

    fprintf(
        stderr,
        "USSR: unknown preprocessor directive: $%s\n",
        directive
    );

    free(directive);

    return -1;
}

int ussr_pp_init(
    ussr_preprocessor_t *pp
)
{
    if (pp == NULL)
        return -1;

    memset(pp, 0, sizeof(*pp));

    pp->current_active = 1;

    pp->output_capacity = 1024;

    pp->output = malloc(pp->output_capacity);

    if (pp->output == NULL)
    {
        memset(pp, 0, sizeof(*pp));
        return -1;
    }

    pp->output[0] = '\0';

    return 0;
}

void ussr_pp_clear_output(
    ussr_preprocessor_t *pp
)
{
    if (pp == NULL)
        return;

    pp->output_length = 0;

    if (pp->output != NULL)
        pp->output[0] = '\0';
}

void ussr_pp_cleanup(
    ussr_preprocessor_t *pp
)
{
    ussr_pp_define_t *define;
    ussr_pp_define_t *next;
    size_t i;

    if (pp == NULL)
        return;

    define = pp->defines;

    while (define != NULL)
    {
        next = define->next;

        free(define->name);
        free(define->value);
        free(define);

        define = next;
    }

    for (i = 0; i < pp->include_depth; ++i)
        free(pp->include_stack[i]);

    free(pp->include_stack);
    free(pp->output);

    memset(pp, 0, sizeof(*pp));
}

int ussr_pp_process_line(
    ussr_preprocessor_t *pp,
    const char *line
)
{
    const char *cursor;
    int result;

    if (pp == NULL || line == NULL)
        return -1;

    cursor = pp_skip_space(line);

    if (*cursor == '$')
    {
        return pp_process_directive(pp, cursor);
    }

    /*
     * Lines belonging to an inactive conditional block
     * are discarded. Preprocessor directives are still
     * processed above so nested conditionals work.
     */
    if (!pp->current_active)
        return 0;

    result = pp_append(pp, line);

    if (result != 0)
        return -1;

    if (line[0] == '\0' ||
        line[strlen(line) - 1] != '\n')
    {
        if (pp_append(pp, "\n") != 0)
            return -1;
    }

    return 0;
}

int ussr_pp_process_file(
    ussr_preprocessor_t *pp,
    const char *filename
)
{
    FILE *file;
    char line[4096];
    int result;

    if (pp == NULL || filename == NULL)
        return -1;

    if (pp_push_include(pp, filename) != 0)
        return -1;

    file = fopen(filename, "r");

    if (file == NULL)
    {
        fprintf(
            stderr,
            "USSR: cannot open included file '%s'\n",
            filename
        );

        pp_pop_include(pp);

        return -1;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        result = ussr_pp_process_line(pp, line);

        if (result != 0)
        {
            fclose(file);
            pp_pop_include(pp);

            return -1;
        }
    }

    if (ferror(file))
    {
        fprintf(
            stderr,
            "USSR: error reading '%s'\n",
            filename
        );

        fclose(file);
        pp_pop_include(pp);

        return -1;
    }

    fclose(file);

    if (pp->conditional_depth != 0 &&
        pp->include_depth == 1)
    {
        fprintf(
            stderr,
            "USSR: unterminated conditional directive\n"
        );

        pp_pop_include(pp);

        return -1;
    }

    pp_pop_include(pp);

    return 0;
}

const char *ussr_pp_output(
    const ussr_preprocessor_t *pp
)
{
    if (pp == NULL)
        return NULL;

    return pp->output;
}