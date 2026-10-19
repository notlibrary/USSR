#ifndef USSR_TULA_H
#define USSR_TULA_H

#include "petersburg.h"

int ussr_tula_dispatch(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
);

void ussr_tula_cleanup(void);

#endif
