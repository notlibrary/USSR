#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bestline.h"
#include "ussr.h"
#include "pp.h"

int yyparse(void);

typedef struct yy_buffer_state *YY_BUFFER_STATE;

extern YY_BUFFER_STATE yy_scan_string(const char *str);
extern void yy_delete_buffer(YY_BUFFER_STATE buffer);
extern ussr_command_list_t *ussr_parsed_program;

static int append_source(
    char **source,
    size_t *source_length,
    size_t *capacity,
    const char *text)
{
    size_t text_length;
    size_t required;
    size_t new_capacity;
    char *new_source;

    if (text == NULL)
        return 0;

    text_length = strlen(text);
    required = *source_length + text_length + 1;

    if (required <= *capacity)
    {
        memcpy(
            *source + *source_length,
            text,
            text_length
        );

        *source_length += text_length;
        (*source)[*source_length] = '\0';

        return 0;
    }

    new_capacity = *capacity == 0 ? 256 : *capacity;

    while (new_capacity < required)
        new_capacity *= 2;

    new_source = realloc(*source, new_capacity);

    if (new_source == NULL)
    {
        fprintf(stderr, "USSR: out of memory\n");
        return -1;
    }

    *source = new_source;
    *capacity = new_capacity;

    memcpy(
        *source + *source_length,
        text,
        text_length
    );

    *source_length += text_length;
    (*source)[*source_length] = '\0';

    return 0;
}

static int count_brackets(
    const char *text,
    int bracket_depth)
{
    size_t i;
    int in_string;
    int escaped;

    in_string = 0;
    escaped = 0;

    if (text == NULL)
        return bracket_depth;

    for (i = 0; text[i] != '\0'; ++i)
    {
        char c;

        c = text[i];

        if (in_string)
        {
            if (escaped)
            {
                escaped = 0;
                continue;
            }

            if (c == '\\')
            {
                escaped = 1;
                continue;
            }

            if (c == '"')
                in_string = 0;

            continue;
        }

        if (c == '"')
        {
            in_string = 1;
            continue;
        }

        if (c == '[')
            ++bracket_depth;
        else if (c == ']' && bracket_depth > 0)
            --bracket_depth;
    }

    return bracket_depth;
}

static int parse_and_execute(
    const char *source)
{
    YY_BUFFER_STATE buffer;
    int result;

    if (source == NULL || source[0] == '\0')
        return 0;

    buffer = yy_scan_string(source);

    if (buffer == NULL)
    {
        fprintf(stderr,
                "USSR: could not create parser buffer\n");
        return -1;
    }

    ussr_parsed_program = NULL;

    result = yyparse();

    yy_delete_buffer(buffer);

    if (result != 0)
    {
        ussr_parsed_program = NULL;
        return -1;
    }

    if (ussr_parsed_program != NULL)
    {
        result = ussr_execute_program(ussr_parsed_program);

        ussr_command_list_free(ussr_parsed_program);
        ussr_parsed_program = NULL;

        return result;
    }

    return 0;
}

static int run_file(const char *filename)
{
    ussr_preprocessor_t pp;
    int result;

    if (ussr_pp_init(&pp) != 0)
    {
        fprintf(stderr,
                "USSR: could not initialize preprocessor\n");
        return EXIT_FAILURE;
    }

    result = ussr_pp_process_file(&pp, filename);

    if (result != 0)
    {
        ussr_pp_cleanup(&pp);
        return EXIT_FAILURE;
    }

    result = parse_and_execute(ussr_pp_output(&pp));

    ussr_pp_cleanup(&pp);

    return result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int run_repl(void)
{
    ussr_preprocessor_t pp;

    char *line;
    char *source;

    size_t source_length;
    size_t capacity;
    size_t line_length;

    int bracket_depth;
    int result;

    printf("USSR Unified Shell Script REPL\n");
    printf("USSR v0.1\n");
    printf("Enter a command list or press Ctrl-D to exit.\n\n");

    if (ussr_pp_init(&pp) != 0)
    {
        fprintf(stderr,
                "USSR: could not initialize preprocessor\n");
        return EXIT_FAILURE;
    }

    source = NULL;
    source_length = 0;
    capacity = 0;
    bracket_depth = 0;

    while ((line = bestline(
                bracket_depth > 0 ? "... " : "ussr> ")) != NULL)
    {
        const char *processed;

        line_length = strlen(line);

        if (line_length == 0)
        {
            bestlineFree(line);
            continue;
        }

        bestlineHistoryAdd(line);

        /*
         * Send exactly one physical input line to the
         * preprocessor.
         *
         * Directives such as:
         *
         *     $define SQUARE(x) {x*x}
         *     $ifdef DEBUG
         *     $endif
         *
         * are consumed here and do not reach Bison.
         */
        result = ussr_pp_process_line(&pp, line);

        bestlineFree(line);

        if (result != 0)
        {
            /*
             * A preprocessor error belongs to this input
             * unit. Reset the parser accumulation, but keep
             * the preprocessor itself alive.
             */
            source_length = 0;
            bracket_depth = 0;

            if (source != NULL)
                source[0] = '\0';

            ussr_pp_clear_output(&pp);
            continue;
        }

        /*
         * Get only the text generated by this call to
         * ussr_pp_process_line().
         */
        processed = ussr_pp_output(&pp);

        if (processed == NULL || processed[0] == '\0')
        {
            /*
             * This was probably a preprocessor directive.
             *
             * The directive has already changed persistent
             * preprocessor state, so simply continue reading.
             */
            ussr_pp_clear_output(&pp);
            continue;
        }

        /*
         * Add preprocessed text, rather than the original
         * source line, to the parser input.
         *
         * For example:
         *
         *     $define SQUARE(x) {x*x}
         *
         *     print(out): SQUARE(5)
         *
         * becomes:
         *
         *     print(out): {5*5}
         */
        if (append_source(
                &source,
                &source_length,
                &capacity,
                processed) != 0)
        {
            ussr_pp_clear_output(&pp);
            free(source);
            ussr_pp_cleanup(&pp);
            return EXIT_FAILURE;
        }

        /*
         * Count command-list brackets in the PREPROCESSED
         * text. This is important because a macro can itself
         * produce parser input.
         */
        bracket_depth = count_brackets(
            processed,
            bracket_depth
        );

        ussr_pp_clear_output(&pp);

        /*
         * Do not invoke Bison until the complete command list
         * has been collected.
         */
        if (bracket_depth > 0)
            continue;

        result = parse_and_execute(source);

        if (result != 0)
        {
            source_length = 0;

            if (source != NULL)
                source[0] = '\0';

            continue;
        }

        source_length = 0;

        if (source != NULL)
            source[0] = '\0';
    }

    free(source);
    ussr_pp_cleanup(&pp);

    printf("\n");

    return EXIT_SUCCESS;
}

int main(int argc, char **argv)
{
    int result;

    ussr_init();

    if (argc > 1)
        result = run_file(argv[1]);
    else
        result = run_repl();

    ussr_cleanup();

    return result;
}