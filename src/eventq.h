#ifndef USSR_EVENTQ_H
#define USSR_EVENTQ_H

#include <stddef.h>
#include <stdint.h>

#include "process.h"

/*
 * Event queue — the USSR analog of the FreeBSD kqueue.
 *
 * Fresh implementation, no libevent/libev: a flat event list plus a
 * platform wait primitive. Two event kinds exist:
 *
 *   TIMER  - fires at a monotonic millisecond deadline (sleep())
 *   CHILD  - fires when a forked/exec'd child process exits; carries
 *            the child's exit code (async external commands)
 *
 * The scheduler registers events on behalf of blocked processes,
 * then, when no process is runnable, calls ussr_eq_dispatch() with a
 * timeout bounded by the nearest timer deadline. Dispatch reaps
 * children with the non-blocking process-layer poll (waitpid
 * WNOHANG / WaitForMultipleObjects) and reports everything that
 * fired; the scheduler wakes the owning processes.
 *
 * Waiting itself uses only OS primitives: poll()/WaitForMultipleObjects
 * on Windows, poll()+waitpid on POSIX. On WASM there are no child
 * processes, so dispatch degenerates to timer expiry checks.
 */

typedef enum
{
    USSR_EQ_TIMER = 1,
    USSR_EQ_CHILD = 2
} ussr_eq_kind_t;

typedef struct
{
    ussr_eq_kind_t kind;
    long owner_pid;      /* USSR process that registered the event */
    int exit_code;       /* CHILD only */
    uint64_t token;      /* registration token */
} ussr_eq_event_t;

void ussr_eq_init(void);
void ussr_eq_cleanup(void);

/* Monotonic clock in milliseconds. */
uint64_t ussr_eq_now_ms(void);

/* Registration. Tokens are nonzero and unique per process lifetime. */
uint64_t ussr_eq_add_timer(long owner_pid, uint64_t deadline_ms);
uint64_t ussr_eq_add_child(long owner_pid, ussr_child_t child);

/* Cancel a pending registration. Returns 0 when found. */
int ussr_eq_cancel(uint64_t token);

/* Non-zero while any event is registered. */
int ussr_eq_pending(void);

/* Milliseconds until the nearest timer deadline, 0 when overdue,
 * -1 when no timers are registered. */
long ussr_eq_next_timeout_ms(void);

/*
 * Wait up to timeout_ms for events, then move every fired event into
 * out[] (capacity max). Returns the number of fired events. A
 * timeout_ms of 0 performs a pure non-blocking poll.
 */
size_t ussr_eq_dispatch(
    int timeout_ms,
    ussr_eq_event_t *out,
    size_t max
);

#endif
