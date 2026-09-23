#ifndef USSR_AUTOCOMPLETE_H
#define USSR_AUTOCOMPLETE_H

#include "bestline.h"

#ifdef __cplusplus
extern "C" {
#endif

#define USSR_AUTOCOMPLETE_MAX_CANDIDATES 128
#define USSR_AUTOCOMPLETE_MAX_HISTORY 256
#define USSR_AUTOCOMPLETE_MAX_SOURCE 8192

/*
 * Autonomous REPL completion engine.
 *
 * The engine deliberately knows nothing about execution.  It consumes
 * editable REPL text, a source excerpt, and locally observed history and
 * filesystem data, then gives Bestline candidate strings.
 */
void ussr_autocomplete_init(void);
void ussr_autocomplete_cleanup(void);

/* The currently accumulated REPL source, used as local context. */
void ussr_autocomplete_set_source(const char *source);

/* Record a successfully submitted REPL line for empirical ranking. */
void ussr_autocomplete_record_history(const char *line);

/* Bestline callback. Register this only while running the REPL. */
void ussr_autocomplete_callback(
    const char *line,
    int cursor,
    bestlineCompletions *completions
);

#ifdef __cplusplus
}
#endif

#endif
