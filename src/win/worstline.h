#ifndef USSR_WORSTLINE_H
#define USSR_WORSTLINE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void(worstlineCompletionCallback)(
    const char *line,
    size_t cursor,
    char ***matches,
    size_t *match_count
);

void worstlineSetCompletionCallback(
    worstlineCompletionCallback *callback
);

/* Called for Ctrl+letter (prefix -1, code 1..26) and for extended keys
 * (prefix 0x00 or 0xE0, code = the next _getch() byte). A non-NULL
 * result replaces the typed line and is submitted at once. */
typedef const char *(worstlineKeyCallback)(int prefix, int code);

void worstlineSetKeyCallback(worstlineKeyCallback *callback);

char *worstline(const char *prompt);
void worstlineHistoryAdd(const char *line);
void worstlineFree(char *line);

#ifdef __cplusplus
}
#endif

#endif
