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
} ussr_preprocessor_t;

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

#endif