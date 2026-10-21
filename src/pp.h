#ifndef USSR_PP_H
#define USSR_PP_H

#include <stddef.h>

#define USSR_PP_MAX_INCLUDE_DEPTH 32

typedef struct ussr_pp_define_t
{
    char *name;
    char *value;
    struct ussr_pp_define_t *next;
} ussr_pp_define_t;

typedef struct ussr_preprocessor_t
{
    char *output;
    size_t output_length;
    size_t output_capacity;

    ussr_pp_define_t *defines;

    char **include_stack;
    size_t include_depth;
    size_t include_capacity;

    int conditional_active[USSR_PP_MAX_INCLUDE_DEPTH];
    int conditional_seen_else[USSR_PP_MAX_INCLUDE_DEPTH];
    size_t conditional_depth;

    int current_active;

    /* Canonical paths of files that declared $once. */
    char **once_files;
    size_t once_count;

    /*
     * Where every output line came from, for diagnostics: output line
     * n (1-based) was line line_numbers[n-1] of map_files[line_files[n-1]].
     * map_length is how much of output has been mapped so far.
     */
    char **map_files;
    size_t map_file_count;
    size_t *line_files;
    int *line_numbers;
    size_t line_count;
    size_t line_capacity;
    size_t map_length;

    /* Line being read in the innermost file, or the count of lines
     * given directly to ussr_pp_process_line (the REPL). */
    int current_line;
} ussr_preprocessor_t;

typedef struct ussr_pp_macro_t
{
    char *name;

    char **parameters;
    size_t parameter_count;

    char *replacement;

    struct ussr_pp_macro_t *next;
} ussr_pp_macro_t;

int ussr_pp_init(ussr_preprocessor_t *pp);

void ussr_pp_cleanup(ussr_preprocessor_t *pp);

int ussr_pp_process_file(
    ussr_preprocessor_t *pp,
    const char *filename
);

int ussr_pp_process_line(
    ussr_preprocessor_t *pp,
    const char *line
);

const char *ussr_pp_output(
    const ussr_preprocessor_t *pp
);

void ussr_pp_clear_output(
    ussr_preprocessor_t *pp
);

/*
 * Maps a line of ussr_pp_output() (1-based) to the file and line it came
 * from. Lines given directly to ussr_pp_process_line are reported as
 * "<stdin>". The file name lives as long as the preprocessor. Returns 0,
 * or -1 if the line is unknown. The context is a ussr_preprocessor_t, so
 * the function can be used as a ussr_line_lookup_t.
 */
int ussr_pp_locate(
    const void *pp,
    int output_line,
    const char **file,
    int *line
);

#endif