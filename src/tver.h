#ifndef USSR_TVER_H
#define USSR_TVER_H

#include "petersburg.h"

int ussr_tver_dispatch(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
);

void ussr_tver_cleanup(void);

#endif
