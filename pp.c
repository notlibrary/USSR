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
    char *result,
    size_t result_size
)
{
    if (filename == NULL ||
        result == NULL ||
        result_size == 0)
    {
        return -1;
    }

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
     * Relative includes are currently resolved against
     * the process working directory.
     *
     * This can later be changed to resolve against the
     * directory of the file containing the directive.
     */
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

static int pp_process_include(
    ussr_preprocessor_t *pp,
    const char *filename
)
{
    char resolved[PATH_MAX];

    if (pp_resolve_include_path(
            filename,
            resolved,
            sizeof(resolved)
        ) != 0)
    {
        fprintf(
            stderr,
            "USSR: include path is too long: %s\n",
            filename
        );

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

    cursor = pp_skip_space(text);

    if (strncmp(cursor, "defined", 7) != 0)
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

    while (*cursor != '\0' &&
           (isalnum((unsigned char)*cursor) ||
            *cursor == '_'))
    {
        ++cursor;
    }

    length = (size_t)(cursor - start);

    if (length == 0)
    {
        fprintf(
            stderr,
            "USSR: expected identifier in defined(...)\n"
        );

        return -1;
    }

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

    result = pp_is_defined(pp, name);

    free(name);

    return result;
}

static int pp_is_identifier_start(char c)
{
    return isalpha((unsigned char)c) || c == '_';
}

static int pp_is_identifier_char(char c)
{
    return isalnum((unsigned char)c) || c == '_';
}

static int pp_macro_signature(
    const ussr_pp_define_t *define,
    const char *name,
    char ***parameters_out,
    size_t *parameter_count_out
)
{
    const char *open;
    const char *cursor;
    const char *start;
    char **parameters;
    size_t count;
    size_t capacity;
    size_t length;
    char *parameter;
    char **new_parameters;

    if (define == NULL || name == NULL ||
        parameters_out == NULL || parameter_count_out == NULL)
    {
        return -1;
    }

    *parameters_out = NULL;
    *parameter_count_out = 0;

    open = strchr(define->name, '(');

    if (open == NULL)
        return 0;

    if ((size_t)(open - define->name) != strlen(name) ||
        strncmp(define->name, name, (size_t)(open - define->name)) != 0)
    {
        return 0;
    }

    cursor = open + 1;
    parameters = NULL;
    count = 0;
    capacity = 0;

    cursor = pp_skip_space(cursor);

    if (*cursor == ')')
    {
        if (cursor[1] != '\0')
            return 0;

        *parameters_out = NULL;
        *parameter_count_out = 0;
        return 1;
    }

    for (;;)
    {
        start = cursor;

        while (*cursor != '\0' &&
               pp_is_identifier_char(*cursor))
        {
            ++cursor;
        }

        length = (size_t)(cursor - start);

        if (length == 0)
            goto invalid;

        parameter = pp_strndup(start, length);

        if (parameter == NULL)
            goto error;

        if (count >= capacity)
        {
            capacity = capacity == 0 ? 4 : capacity * 2;

            new_parameters = realloc(
                parameters,
                capacity * sizeof(*new_parameters)
            );

            if (new_parameters == NULL)
            {
                free(parameter);
                goto error;
            }

            parameters = new_parameters;
        }

        parameters[count++] = parameter;

        cursor = pp_skip_space(cursor);

        if (*cursor == ')')
        {
            if (cursor[1] != '\0')
                goto invalid;

            *parameters_out = parameters;
            *parameter_count_out = count;
            return 1;
        }

        if (*cursor != ',')
            goto invalid;

        ++cursor;
        cursor = pp_skip_space(cursor);
    }

invalid:
    fprintf(
        stderr,
        "USSR: invalid macro parameter list: %s\n",
        define->name
    );

error:
    if (parameters != NULL)
    {
        size_t i;

        for (i = 0; i < count; ++i)
            free(parameters[i]);

        free(parameters);
    }

    return -1;
}

static void pp_free_macro_parameters(
    char **parameters,
    size_t count
)
{
    size_t i;

    if (parameters == NULL)
        return;

    for (i = 0; i < count; ++i)
        free(parameters[i]);

    free(parameters);
}

static ussr_pp_define_t *pp_find_function_define(
    ussr_preprocessor_t *pp,
    const char *name,
    char ***parameters_out,
    size_t *parameter_count_out
)
{
    ussr_pp_define_t *define;
    char **parameters;
    size_t parameter_count;
    int result;

    if (parameters_out == NULL || parameter_count_out == NULL)
        return NULL;

    *parameters_out = NULL;
    *parameter_count_out = 0;

    for (define = pp->defines;
         define != NULL;
         define = define->next)
    {
        if (strchr(define->name, '(') == NULL)
            continue;

        result = pp_macro_signature(
            define,
            name,
            &parameters,
            &parameter_count
        );

        if (result == 1)
        {
            *parameters_out = parameters;
            *parameter_count_out = parameter_count;
            return define;
        }

        if (result < 0)
            return NULL;
    }

    return NULL;
}

static char *pp_trimmed_strndup(
    const char *src,
    size_t length
)
{
    size_t start;

    if (src == NULL)
        return NULL;

    start = 0;

    while (start < length &&
           isspace((unsigned char)src[start]))
    {
        ++start;
    }

    while (length > start &&
           isspace((unsigned char)src[length - 1]))
    {
        --length;
    }

    return pp_strndup(src + start, length - start);
}

static int pp_extract_macro_arguments(
    const char *open,
    char ***arguments_out,
    size_t *argument_count_out,
    const char **after_out
)
{
    const char *cursor;
    const char *start;
    char **arguments;
    char **new_arguments;
    size_t count;
    size_t capacity;
    size_t length;
    int paren_depth;
    int brace_depth;
    int in_string;
    int escaped;
    char *argument;

    if (open == NULL || arguments_out == NULL ||
        argument_count_out == NULL || after_out == NULL ||
        *open != '(')
    {
        return -1;
    }

    arguments = NULL;
    count = 0;
    capacity = 0;
    cursor = open + 1;
    start = cursor;
    paren_depth = 0;
    brace_depth = 0;
    in_string = 0;
    escaped = 0;

    while (*cursor != '\0')
    {
        char c;

        c = *cursor;

        if (in_string)
        {
            if (escaped)
            {
                escaped = 0;
            }
            else if (c == '\\')
            {
                escaped = 1;
            }
            else if (c == '"')
            {
                in_string = 0;
            }

            ++cursor;
            continue;
        }

        if (c == '"')
        {
            in_string = 1;
            ++cursor;
            continue;
        }

        if (c == '(')
        {
            ++paren_depth;
            ++cursor;
            continue;
        }

        if (c == ')')
        {
            if (paren_depth > 0)
            {
                --paren_depth;
                ++cursor;
                continue;
            }

            if (brace_depth != 0)
                goto invalid;

            length = (size_t)(cursor - start);

            if (length != 0 || count != 0)
            {
                argument = pp_trimmed_strndup(start, length);

                if (argument == NULL)
                    goto error;

                if (count >= capacity)
                {
                    capacity = capacity == 0 ? 4 : capacity * 2;

                    new_arguments = realloc(
                        arguments,
                        capacity * sizeof(*new_arguments)
                    );

                    if (new_arguments == NULL)
                    {
                        free(argument);
                        goto error;
                    }

                    arguments = new_arguments;
                }

                arguments[count++] = argument;
            }

            *arguments_out = arguments;
            *argument_count_out = count;
            *after_out = cursor + 1;
            return 0;
        }

        if (c == '{')
        {
            ++brace_depth;
            ++cursor;
            continue;
        }

        if (c == '}' && brace_depth > 0)
        {
            --brace_depth;
            ++cursor;
            continue;
        }

        if (c == ',' && paren_depth == 0 && brace_depth == 0)
        {
            length = (size_t)(cursor - start);
            argument = pp_trimmed_strndup(start, length);

            if (argument == NULL)
                goto error;

            if (count >= capacity)
            {
                capacity = capacity == 0 ? 4 : capacity * 2;

                new_arguments = realloc(
                    arguments,
                    capacity * sizeof(*new_arguments)
                );

                if (new_arguments == NULL)
                {
                    free(argument);
                    goto error;
                }

                arguments = new_arguments;
            }

            arguments[count++] = argument;
            ++cursor;
            start = cursor;
            continue;
        }

        ++cursor;
    }

invalid:
    fprintf(
        stderr,
        "USSR: unterminated macro invocation\n"
    );

error:
    if (arguments != NULL)
    {
        size_t i;

        for (i = 0; i < count; ++i)
            free(arguments[i]);

        free(arguments);
    }

    return -1;
}

static int pp_substitute_macro(
    const char *body,
    char **parameters,
    char **arguments,
    size_t parameter_count,
    ussr_preprocessor_t *pp
)
{
    const char *cursor;
    const char *start;
    char *name;
    size_t length;
    size_t i;
    int in_string;
    int escaped;

    cursor = body;
    in_string = 0;
    escaped = 0;

    while (*cursor != '\0')
    {
        if (in_string)
        {
            char one[2];

            one[0] = *cursor;
            one[1] = '\0';

            if (pp_append(pp, one) != 0)
                return -1;

            if (escaped)
                escaped = 0;
            else if (*cursor == '\\')
                escaped = 1;
            else if (*cursor == '"')
                in_string = 0;

            ++cursor;
            continue;
        }

        if (*cursor == '"')
        {
            in_string = 1;

            if (pp_append(pp, "\"") != 0)
                return -1;

            ++cursor;
            continue;
        }

        if (!pp_is_identifier_start(*cursor))
        {
            char one[2];

            one[0] = *cursor;
            one[1] = '\0';

            if (pp_append(pp, one) != 0)
                return -1;

            ++cursor;
            continue;
        }

        start = cursor;
        ++cursor;

        while (*cursor != '\0' &&
               pp_is_identifier_char(*cursor))
        {
            ++cursor;
        }

        length = (size_t)(cursor - start);
        name = pp_strndup(start, length);

        if (name == NULL)
            return -1;

        for (i = 0; i < parameter_count; ++i)
        {
            if (strcmp(name, parameters[i]) == 0)
                break;
        }

        if (i < parameter_count)
        {
            if (pp_append(pp, arguments[i]) != 0)
            {
                free(name);
                return -1;
            }
        }
        else if (pp_append(pp, name) != 0)
        {
            free(name);
            return -1;
        }

        free(name);
    }

    return 0;
}

static int pp_expand_text_depth(
    ussr_preprocessor_t *pp,
    const char *text,
    unsigned int depth
)
{
    const char *cursor;
    const char *start;
    const char *string_start;
    const char *after;
    char *name;
    char *string;
    char **parameters;
    char **arguments;
    size_t length;
    size_t parameter_count;
    size_t argument_count;
    int escaped;
    ussr_pp_define_t *define;
    ussr_pp_define_t *function_define;

    if (pp == NULL || text == NULL)
        return -1;

    if (depth > 64)
    {
        fprintf(
            stderr,
            "USSR: macro expansion too deep\n"
        );
        return -1;
    }

    cursor = text;

    while (*cursor != '\0')
    {
        if (*cursor == '"')
        {
            string_start = cursor;
            ++cursor;
            escaped = 0;

            while (*cursor != '\0')
            {
                if (escaped)
                {
                    escaped = 0;
                    ++cursor;
                    continue;
                }

                if (*cursor == '\\')
                {
                    escaped = 1;
                    ++cursor;
                    continue;
                }

                if (*cursor == '"')
                {
                    ++cursor;
                    break;
                }

                ++cursor;
            }

            length = (size_t)(cursor - string_start);
            string = pp_strndup(string_start, length);

            if (string == NULL)
                return -1;

            if (pp_append(pp, string) != 0)
            {
                free(string);
                return -1;
            }

            free(string);
            continue;
        }

        if (!pp_is_identifier_start(*cursor))
        {
            char one[2];

            one[0] = *cursor;
            one[1] = '\0';

            if (pp_append(pp, one) != 0)
                return -1;

            ++cursor;
            continue;
        }

        start = cursor;
        ++cursor;

        while (*cursor != '\0' &&
               pp_is_identifier_char(*cursor))
        {
            ++cursor;
        }

        length = (size_t)(cursor - start);
        name = pp_strndup(start, length);

        if (name == NULL)
            return -1;

        define = pp_find_define(pp, name);

        /*
         * Function-like macros have a name containing the complete
         * signature, for example SQUARE(x).  Look for one only when
         * the identifier is immediately followed by an invocation.
         */
        function_define = NULL;
        parameters = NULL;
        parameter_count = 0;

        {
            const char *lookahead;

            lookahead = pp_skip_space(cursor);

            if (*lookahead == '(')
            {
                function_define = pp_find_function_define(
                    pp,
                    name,
                    &parameters,
                    &parameter_count
                );

                if (function_define != NULL)
                {
                    arguments = NULL;
                    argument_count = 0;

                    if (pp_extract_macro_arguments(
                            lookahead,
                            &arguments,
                            &argument_count,
                            &after
                        ) != 0)
                    {
                        pp_free_macro_parameters(
                            parameters,
                            parameter_count
                        );
                        free(name);
                        return -1;
                    }

                    if (argument_count != parameter_count)
                    {
                        fprintf(
                            stderr,
                            "USSR: macro '%s' expects %zu argument%s, got %zu\n",
                            name,
                            parameter_count,
                            parameter_count == 1 ? "" : "s",
                            argument_count
                        );

                        pp_free_macro_parameters(
                            parameters,
                            parameter_count
                        );

                        for (length = 0;
                             length < argument_count;
                             ++length)
                        {
                            free(arguments[length]);
                        }

                        free(arguments);
                        free(name);
                        return -1;
                    }

                    {
                        ussr_preprocessor_t temporary;

                        memset(&temporary, 0, sizeof(temporary));
                        temporary.defines = pp->defines;
                        temporary.current_active = 1;
                        temporary.output_capacity = 1024;
                        temporary.output = malloc(temporary.output_capacity);

                        if (temporary.output == NULL)
                        {
                            pp_free_macro_parameters(
                                parameters,
                                parameter_count
                            );

                            for (length = 0;
                                 length < argument_count;
                                 ++length)
                            {
                                free(arguments[length]);
                            }

                            free(arguments);
                            free(name);
                            return -1;
                        }

                        temporary.output[0] = '\0';

                        if (pp_substitute_macro(
                                function_define->value,
                                parameters,
                                arguments,
                                parameter_count,
                                &temporary
                            ) != 0 ||
                            pp_expand_text_depth(
                                pp,
                                temporary.output,
                                depth + 1
                            ) != 0)
                        {
                            free(temporary.output);

                            pp_free_macro_parameters(
                                parameters,
                                parameter_count
                            );

                            for (length = 0;
                                 length < argument_count;
                                 ++length)
                            {
                                free(arguments[length]);
                            }

                            free(arguments);
                            free(name);
                            return -1;
                        }

                        free(temporary.output);
                    }

                    cursor = after;

                    pp_free_macro_parameters(
                        parameters,
                        parameter_count
                    );

                    for (length = 0;
                         length < argument_count;
                         ++length)
                    {
                        free(arguments[length]);
                    }

                    free(arguments);
                    free(name);
                    continue;
                }

                pp_free_macro_parameters(parameters, parameter_count);
            }
        }

        if (define != NULL && strchr(define->name, '(') == NULL)
        {
            if (pp_expand_text_depth(
                    pp,
                    define->value,
                    depth + 1
                ) != 0)
            {
                free(name);
                return -1;
            }
        }
        else if (pp_append(pp, name) != 0)
        {
            free(name);
            return -1;
        }

        free(name);
    }

    return 0;
}

static int pp_expand_text(
    ussr_preprocessor_t *pp,
    const char *text
)
{
    return pp_expand_text_depth(pp, text, 0);
}

static int pp_parse_define(
    ussr_preprocessor_t *pp,
    const char *text
)
{
    const char *cursor;
    const char *name_start;
    const char *value_start;
    const char *signature_end;
    char *name;
    char *value;
    size_t name_length;
    size_t signature_length;
    int result;

    cursor = pp_skip_space(text);
    name_start = cursor;

    if (!pp_is_identifier_start(*cursor))
    {
        fprintf(
            stderr,
            "USSR: $define requires a name\n"
        );
        return -1;
    }

    ++cursor;

    while (*cursor != '\0' &&
           pp_is_identifier_char(*cursor))
    {
        ++cursor;
    }

    name_length = (size_t)(cursor - name_start);

    cursor = pp_skip_space(cursor);

    if (*cursor == '(')
    {
        const char *scan;
        int depth;
        int in_string;
        int escaped;

        scan = cursor;
        depth = 0;
        in_string = 0;
        escaped = 0;

        while (*scan != '\0')
        {
            char c;

            c = *scan;

            if (in_string)
            {
                if (escaped)
                    escaped = 0;
                else if (c == '\\')
                    escaped = 1;
                else if (c == '"')
                    in_string = 0;

                ++scan;
                continue;
            }

            if (c == '"')
            {
                in_string = 1;
                ++scan;
                continue;
            }

            if (c == '(')
                ++depth;
            else if (c == ')')
            {
                --depth;

                if (depth == 0)
                    break;
            }

            ++scan;
        }

        if (*scan != ')')
        {
            fprintf(
                stderr,
                "USSR: unterminated $define parameter list\n"
            );
            return -1;
        }

        signature_end = scan + 1;
        signature_length = (size_t)(signature_end - name_start);
        name = pp_strndup(name_start, signature_length);

        if (name == NULL)
            return -1;

        /* Validate the stored function-like signature. */
        {
            ussr_pp_define_t temporary;
            char *base_name;
            char **parameters = NULL;
            size_t parameter_count = 0;

            base_name = pp_strndup(name_start, name_length);

            if (base_name == NULL)
            {
                free(name);
                return -1;
            }

            memset(&temporary, 0, sizeof(temporary));
            temporary.name = name;

            result = pp_macro_signature(
                &temporary,
                base_name,
                &parameters,
                &parameter_count
            );

            free(base_name);
            pp_free_macro_parameters(parameters, parameter_count);

            if (result != 1)
            {
                free(name);
                return -1;
            }
        }

        cursor = pp_skip_space(signature_end);
    }
    else
    {
        name = pp_strndup(name_start, name_length);

        if (name == NULL)
            return -1;
    }

    value_start = cursor;

    value = pp_strdup(value_start);

    if (value == NULL)
    {
        free(name);
        return -1;
    }

    /* A directive line normally ends with a newline from fgets(). */
    {
        size_t value_length;

        value_length = strlen(value);

        while (value_length > 0 &&
               (value[value_length - 1] == '\n' ||
                value[value_length - 1] == '\r'))
        {
            value[--value_length] = '\0';
        }
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

    result = pp_expand_text(pp, line);

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