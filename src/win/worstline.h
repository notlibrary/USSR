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

char *worstline(const char *prompt);
void worstlineHistoryAdd(const char *line);
void worstlineFree(char *line);

#ifdef __cplusplus
}
#endif

#endif
