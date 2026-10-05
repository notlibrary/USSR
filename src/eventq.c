#include "eventq.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__EMSCRIPTEN__)
#include <time.h>
#else
#include <errno.h>
#include <poll.h>
#include <time.h>
#endif

/*
 * Flat event list. Both kinds are rare (one per blocked process), so
 * a pair of dynamic arrays with linear scans is entirely sufficient
 * and keeps the implementation dependency-free.
 */

typedef struct
{
    long owner_pid;
    uint64_t token;
    uint64_t deadline_ms;
} eq_timer_t;

typedef struct
{
    long owner_pid;
    uint64_t token;
    ussr_child_t child;
    int reaped;        /* non-zero once the exit code below is final */
    int exit_code;
} eq_child_t;

static eq_timer_t *eq_timers = NULL;
static size_t eq_timer_count = 0;
static size_t eq_timer_capacity = 0;

static eq_child_t *eq_children = NULL;
static size_t eq_child_count = 0;
static size_t eq_child_capacity = 0;

static uint64_t eq_next_token = 1;

void ussr_eq_init(void)
{
    eq_timers = NULL;
    eq_timer_count = 0;
    eq_timer_capacity = 0;
    eq_children = NULL;
    eq_child_count = 0;
    eq_child_capacity = 0;
    eq_next_token = 1;
}

void ussr_eq_cleanup(void)
{
    size_t i;

    for (i = 0; i < eq_child_count; ++i)
        ussr_child_close(&eq_children[i].child);

    free(eq_timers);
    free(eq_children);

    eq_timers = NULL;
    eq_children = NULL;
    eq_timer_count = 0;
    eq_child_count = 0;
    eq_timer_capacity = 0;
    eq_child_capacity = 0;
}

uint64_t ussr_eq_now_ms(void)
{
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;

    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

uint64_t ussr_eq_add_timer(long owner_pid, uint64_t deadline_ms)
{
    eq_timer_t *timer;

    if (eq_timer_count == eq_timer_capacity)
    {
        size_t capacity = eq_timer_capacity == 0 ? 8 : eq_timer_capacity * 2;
        eq_timer_t *grown = realloc(eq_timers, capacity * sizeof(*grown));
        if (grown == NULL)
            return 0;
        eq_timers = grown;
        eq_timer_capacity = capacity;
    }

    timer = &eq_timers[eq_timer_count++];
    timer->owner_pid = owner_pid;
    timer->token = eq_next_token++;
    timer->deadline_ms = deadline_ms;

    return timer->token;
}

uint64_t ussr_eq_add_child(long owner_pid, ussr_child_t child)
{
    eq_child_t *entry;

    if (eq_child_count == eq_child_capacity)
    {
        size_t capacity = eq_child_capacity == 0 ? 8 : eq_child_capacity * 2;
        eq_child_t *grown = realloc(eq_children, capacity * sizeof(*grown));
        if (grown == NULL)
            return 0;
        eq_children = grown;
        eq_child_capacity = capacity;
    }

    entry = &eq_children[eq_child_count++];
    entry->owner_pid = owner_pid;
    entry->token = eq_next_token++;
    entry->child = child;
    entry->reaped = 0;
    entry->exit_code = 0;

    return entry->token;
}

int ussr_eq_cancel(uint64_t token)
{
    size_t i;

    for (i = 0; i < eq_timer_count; ++i)
    {
        if (eq_timers[i].token == token)
        {
            memmove(&eq_timers[i], &eq_timers[i + 1],
                    (eq_timer_count - i - 1) * sizeof(*eq_timers));
            --eq_timer_count;
            return 0;
        }
    }

    for (i = 0; i < eq_child_count; ++i)
    {
        if (eq_children[i].token == token)
        {
            ussr_child_close(&eq_children[i].child);
            memmove(&eq_children[i], &eq_children[i + 1],
                    (eq_child_count - i - 1) * sizeof(*eq_children));
            --eq_child_count;
            return 0;
        }
    }

    return -1;
}

int ussr_eq_pending(void)
{
    return eq_timer_count != 0 || eq_child_count != 0;
}

long ussr_eq_next_timeout_ms(void)
{
    uint64_t now;
    uint64_t nearest;
    size_t i;

    if (eq_timer_count == 0)
        return -1;

    now = ussr_eq_now_ms();
    nearest = eq_timers[0].deadline_ms;

    for (i = 1; i < eq_timer_count; ++i)
        if (eq_timers[i].deadline_ms < nearest)
            nearest = eq_timers[i].deadline_ms;

    if (nearest <= now)
        return 0;

    if (nearest - now > (uint64_t)INT32_MAX)
        return INT32_MAX;

    return (long)(nearest - now);
}

/*
 * Sleep without any event sources, in small chunks when children are
 * watched so they can be reaped promptly.
 */
static void eq_platform_sleep(int timeout_ms)
{
#if defined(_WIN32)
    Sleep((DWORD)(timeout_ms < 0 ? 0 : timeout_ms));
#elif defined(__EMSCRIPTEN__)
    (void)timeout_ms;
#else
    struct pollfd none;

    memset(&none, 0, sizeof(none));

    if (timeout_ms > 0)
        poll(&none, 0, timeout_ms);
#endif
}

size_t ussr_eq_dispatch(
    int timeout_ms,
    ussr_eq_event_t *out,
    size_t max
)
{
    size_t fired = 0;
    uint64_t now;
    size_t i;

    if (out == NULL || max == 0)
        return 0;

#if !defined(__EMSCRIPTEN__)
    /*
     * Phase 1: wait. With children registered, wait in bounded chunks
     * so a child exit interrupts the timer sleep, then let phase 3
     * below reap whoever exited.
     */
    if (timeout_ms != 0 && eq_child_count != 0)
    {
        uint64_t deadline = ussr_eq_now_ms() +
            (uint64_t)(timeout_ms < 0 ? 0 : timeout_ms);

        for (;;)
        {
            int any_running = 0;
            int any_exited = 0;

            for (i = 0; i < eq_child_count; ++i)
            {
                int exit_code = 0;
                int poll_result;

                if (eq_children[i].reaped)
                    continue;

                poll_result =
                    ussr_child_poll(&eq_children[i].child, &exit_code);

                if (poll_result != 0)
                {
                    eq_children[i].reaped = 1;
                    eq_children[i].exit_code =
                        poll_result == 1 ? exit_code : -1;
                    any_exited = 1;
                }
                else
                {
                    any_running = 1;
                }
            }

            if (any_exited || !any_running)
                break;

            if (timeout_ms >= 0 && ussr_eq_now_ms() >= deadline)
                break;

            if (timeout_ms < 0 && eq_timer_count != 0 &&
                ussr_eq_next_timeout_ms() == 0)
                break;

#if defined(_WIN32)
            Sleep(5);
#else
            eq_platform_sleep(5);
#endif
        }
    }
    else if (timeout_ms > 0)
    {
        eq_platform_sleep(timeout_ms);
    }
#else
    (void)timeout_ms;
#endif

    /* Phase 2: collect fired timers. */
    now = ussr_eq_now_ms();
    i = 0;
    while (i < eq_timer_count && fired < max)
    {
        if (eq_timers[i].deadline_ms <= now)
        {
            out[fired].kind = USSR_EQ_TIMER;
            out[fired].owner_pid = eq_timers[i].owner_pid;
            out[fired].exit_code = 0;
            out[fired].token = eq_timers[i].token;
            ++fired;

            memmove(&eq_timers[i], &eq_timers[i + 1],
                    (eq_timer_count - i - 1) * sizeof(*eq_timers));
            --eq_timer_count;
            continue;
        }
        ++i;
    }

    /* Phase 3: collect exited children. */
    i = 0;
    while (i < eq_child_count && fired < max)
    {
        int poll_result = 0;

        if (!eq_children[i].reaped)
        {
            int exit_code = 0;
            poll_result = ussr_child_poll(&eq_children[i].child, &exit_code);
            if (poll_result != 0)
            {
                eq_children[i].reaped = 1;
                eq_children[i].exit_code =
                    poll_result == 1 ? exit_code : -1;
            }
        }

        if (eq_children[i].reaped)
        {
            out[fired].kind = USSR_EQ_CHILD;
            out[fired].owner_pid = eq_children[i].owner_pid;
            out[fired].exit_code = eq_children[i].exit_code;
            out[fired].token = eq_children[i].token;
            ++fired;

            memmove(&eq_children[i], &eq_children[i + 1],
                    (eq_child_count - i - 1) * sizeof(*eq_children));
            --eq_child_count;
            continue;
        }
        ++i;
    }

    return fired;
}
