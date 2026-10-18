#ifndef USSR_PETERSBURG_H
#define USSR_PETERSBURG_H

#include "scheduler.h"

int ussr_petersburg_dispatch(
    ussr_sched_program_t *program,
    const char *name,
    const ussr_value_t *values,
    size_t value_count,
    ussr_value_t *out
);

int ussr_petersburg_pump(
    ussr_sched_program_t *program,
    ussr_proc_t *parent,
    int wait_for_events
);
int ussr_petersburg_running(void);
void ussr_petersburg_cleanup(void);

#endif
