#include "autocomplete.h"
#include "completion_fs.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define AC_MAX_TOKEN 1024
#define AC_SCORE_PREFIX 1000.0
#define AC_SCORE_COMMAND 180.0
#define AC_SCORE_HISTORY 120.0
#define AC_SCORE_RECENCY 80.0
#define AC_SCORE_SOURCE 70.0
#define AC_SCORE_FILESYSTEM 60.0
#define AC_SCORE_EXECUTABLE 45.0
#define AC_SCORE_OPTION 55.0

typedef enum
{
    AC_KIND_COMMAND,
    AC_KIND_PATH,
    AC_KIND_HISTORY,
    AC_KIND_OPTION,
    AC_KIND_SOURCE
} ac_kind_t;

typedef struct
{
    char *text;
    double score;
    unsigned history_hits;
    unsigned recency;
    ac_kind_t kind;
} ac_candidate_t;

typedef struct
{
    char history[USSR_AUTOCOMPLETE_MAX_HISTORY][AC_MAX_TOKEN];
    unsigned history_count;
    unsigned serial;
    char source[USSR_AUTOCOMPLETE_MAX_SOURCE];
} ac_state_t;

static ac_state_t state;

static char *ac_strdup(const char *text)
{
    size_t n;
    char *copy;

    if (text == NULL)
        return NULL;

    n = strlen(text);
    copy = malloc(n + 1);
    if (copy != NULL)
        memcpy(copy, text, n + 1);

    return copy;
}

static int ac_case_equal(char a, char b)
{
#if defined(_WIN32)
    return tolower((unsigned char)a) == tolower((unsigned char)b);
#else
    return a == b;
#endif
}

static int ac_prefix_match(const char *text, const char *prefix)
{
    size_t i;

    if (text == NULL || prefix == NULL)
        return 0;

    for (i = 0; prefix[i] != '\0'; ++i)
    {
        if (text[i] == '\0' || !ac_case_equal(text[i], prefix[i]))
            return 0;
    }

    return 1;
}

static size_t ac_common_prefix(const char *a, const char *b)
{
    size_t i = 0;

    while (a[i] != '\0' && b[i] != '\0' &&
           ac_case_equal(a[i], b[i]))
        ++i;

    return i;
}

static int ac_is_word_char(unsigned char c)
{
    return isalnum(c) || c == '_' || c == '.' ||
           c == '/' || c == '\\' || c == '-' || c == ':' ||
           c == '~';
}

static int ac_is_space(unsigned char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static void ac_extract_token(
    const char *line,
    int cursor,
    char *token,
    size_t token_size,
    size_t *start)
{
    size_t end;
    size_t begin;
    size_t n;

    if (token_size == 0)
        return;

    token[0] = '\0';

    if (line == NULL)
    {
        if (start != NULL)
            *start = 0;
        return;
    }

    n = strlen(line);
    if (cursor < 0 || (size_t)cursor > n)
        cursor = (int)n;

    end = (size_t)cursor;
    begin = end;

    while (begin > 0 && ac_is_word_char((unsigned char)line[begin - 1]))
        --begin;

    n = end - begin;
    if (n >= token_size)
        n = token_size - 1;

    memcpy(token, line + begin, n);
    token[n] = '\0';

    if (start != NULL)
        *start = begin;
}

static int ac_in_command_head(const char *line, size_t start)
{
    size_t i;
    size_t command_start;

    if (line == NULL)
        return 0;

    command_start = start;
    while (command_start > 0 &&
           line[command_start - 1] != '\n' &&
           ac_is_space((unsigned char)line[command_start - 1]))
        --command_start;

    /* If this token is immediately after a colon, it is an argument. */
    if (command_start > 0 && line[command_start - 1] == ':')
        return 0;

    /* A command head has no whitespace before its first '(' yet. */
    for (i = command_start; i < start; ++i)
    {
        if (line[i] == ':' || line[i] == '(')
            return 0;
    }

    return 1;
}

static int ac_argument_position(
    const char *line,
    size_t start,
    const char *token)
{
    size_t i;
    int in_string = 0;
    int escaped = 0;
    size_t colon = SIZE_MAX;

    (void)token;

    if (line == NULL)
        return 0;

    for (i = start; i > 0; --i)
    {
        char c = line[i - 1];

        if (c == '\n')
            break;

        if (!in_string && c == ':')
        {
            colon = i - 1;
            break;
        }
    }

    if (colon == SIZE_MAX)
        return 0;

    for (i = colon + 1; i < start; ++i)
    {
        char c = line[i];

        if (in_string)
        {
            if (escaped)
                escaped = 0;
            else if (c == '\\')
                escaped = 1;
            else if (c == '"')
                in_string = 0;
        }
        else if (c == '"')
        {
            in_string = 1;
        }
    }

    return 1;
}

static const char *ac_builtin_commands[] =
{
    "if",
    "do",
    "loop",
    "while",
    "break",
    "continue",
    "return",
    "set",
    "print",
    "add",
    "sub",
    "mul",
    "div",
    "mod",
    "concat",
    "get",
    "random64",
    "seed_random64",
    "time",
    "scan",
    "cd",
    "chain",
    "file",
    "eval",
    NULL
};

static unsigned ac_history_stats(
    const char *candidate,
    unsigned *recency)
{
    unsigned hits = 0;
    unsigned best = 0;
    unsigned i;

    for (i = 0; i < state.history_count; ++i)
    {
        if (strstr(state.history[i], candidate) != NULL)
        {
            ++hits;
            if (state.history_count - i > best)
                best = state.history_count - i;
        }
    }

    if (recency != NULL)
        *recency = best;

    return hits;
}

static int ac_candidate_exists(
    ac_candidate_t *list,
    size_t count,
    const char *text)
{
    size_t i;

    for (i = 0; i < count; ++i)
    {
        if (strcmp(list[i].text, text) == 0)
            return 1;
    }

    return 0;
}

static int ac_add_candidate(
    ac_candidate_t *list,
    size_t *count,
    const char *text,
    double score,
    ac_kind_t kind)
{
    ac_candidate_t *candidate;
    unsigned recency;
    unsigned hits;

    if (text == NULL || text[0] == '\0' ||
        *count >= USSR_AUTOCOMPLETE_MAX_CANDIDATES)
        return 0;

    if (ac_candidate_exists(list, *count, text))
        return 0;

    candidate = &list[*count];
    candidate->text = ac_strdup(text);
    if (candidate->text == NULL)
        return -1;

    hits = ac_history_stats(text, &recency);
    candidate->history_hits = hits;
    candidate->recency = recency;
    candidate->score = score +
        (double)hits * AC_SCORE_HISTORY +
        (double)recency * AC_SCORE_RECENCY;
    candidate->kind = kind;

    ++*count;
    return 0;
}

static void ac_free_candidates(
    ac_candidate_t *list,
    size_t count)
{
    size_t i;

    for (i = 0; i < count; ++i)
        free(list[i].text);
}

static int ac_compare_candidates(
    const void *left,
    const void *right)
{
    const ac_candidate_t *a = left;
    const ac_candidate_t *b = right;

    if (a->score < b->score)
        return 1;
    if (a->score > b->score)
        return -1;

    return strcmp(a->text, b->text);
}

static void ac_add_commands(
    const char *prefix,
    ac_candidate_t *list,
    size_t *count)
{
    size_t i;

    for (i = 0; ac_builtin_commands[i] != NULL; ++i)
    {
        const char *command = ac_builtin_commands[i];
        size_t common;
        double score;

        if (!ac_prefix_match(command, prefix))
            continue;

        common = ac_common_prefix(command, prefix);
        score = AC_SCORE_COMMAND + (double)common * 10.0;

        ac_add_candidate(
            list,
            count,
            command,
            score,
            AC_KIND_COMMAND
        );
    }
}

static void ac_add_history_commands(
    const char *prefix,
    ac_candidate_t *list,
    size_t *count)
{
    unsigned i;

    for (i = 0; i < state.history_count; ++i)
    {
        const char *line = state.history[i];
        size_t begin = 0;
        size_t end = 0;
        char command[AC_MAX_TOKEN];

        while (line[begin] != '\0' && ac_is_space((unsigned char)line[begin]))
            ++begin;

        end = begin;
        while (line[end] != '\0' &&
               !ac_is_space((unsigned char)line[end]) &&
               line[end] != ':' && line[end] != '(')
            ++end;

        if (end <= begin || end - begin >= sizeof(command))
            continue;

        memcpy(command, line + begin, end - begin);
        command[end - begin] = '\0';

        if (!ac_prefix_match(command, prefix))
            continue;

        ac_add_candidate(
            list,
            count,
            command,
            AC_SCORE_COMMAND + AC_SCORE_HISTORY,
            AC_KIND_HISTORY
        );
    }
}

static void ac_add_source_identifiers(
    const char *prefix,
    ac_candidate_t *list,
    size_t *count)
{
    const char *p = state.source;

    while (p != NULL && *p != '\0')
    {
        char word[AC_MAX_TOKEN];
        size_t n = 0;

        while (*p != '\0' &&
               !(isalpha((unsigned char)*p) || *p == '_'))
            ++p;

        while (p[n] != '\0' &&
               (isalnum((unsigned char)p[n]) || p[n] == '_'))
        {
            if (n + 1 < sizeof(word))
                word[n] = p[n];
            ++n;
        }

        if (n != 0 && n < sizeof(word))
        {
            word[n] = '\0';

            if (ac_prefix_match(word, prefix))
            {
                ac_add_candidate(
                    list,
                    count,
                    word,
                    AC_SCORE_SOURCE + (double)n,
                    AC_KIND_SOURCE
                );
            }
        }

        if (*p == '\0')
            break;

        p += n;
    }
}

static void ac_add_filesystem_candidates(
    const char *token,
    ac_candidate_t *list,
    size_t *count)
{
    ussr_completion_fs_entry_t entries[USSR_COMPLETION_FS_MAX_RESULTS];
    size_t entry_count;
    size_t i;

    entry_count = ussr_completion_fs_list(
        token,
        entries,
        sizeof(entries) / sizeof(entries[0])
    );

    for (i = 0; i < entry_count; ++i)
    {
        double score = AC_SCORE_FILESYSTEM;

        if (entries[i].directory)
            score += 15.0;

        if (entries[i].executable)
            score += AC_SCORE_EXECUTABLE;

        ac_add_candidate(
            list,
            count,
            entries[i].path,
            score,
            AC_KIND_PATH
        );
    }
}

static void ac_add_history_options(
    const char *line,
    const char *prefix,
    ac_candidate_t *list,
    size_t *count)
{
    size_t command_end = 0;
    size_t command_begin = 0;
    char command[AC_MAX_TOKEN];
    unsigned i;

    while (line[command_begin] != '\0' &&
           ac_is_space((unsigned char)line[command_begin]))
        ++command_begin;

    command_end = command_begin;
    while (line[command_end] != '\0' &&
           !ac_is_space((unsigned char)line[command_end]) &&
           line[command_end] != ':' && line[command_end] != '(')
        ++command_end;

    if (command_end <= command_begin ||
        command_end - command_begin >= sizeof(command))
        return;

    memcpy(command, line + command_begin, command_end - command_begin);
    command[command_end - command_begin] = '\0';

    for (i = 0; i < state.history_count; ++i)
    {
        const char *p = state.history[i];
        char history_command[AC_MAX_TOKEN];
        size_t end = 0;
        size_t begin = 0;

        while (p[begin] != '\0' && ac_is_space((unsigned char)p[begin]))
            ++begin;

        while (p[end + begin] != '\0' &&
               !ac_is_space((unsigned char)p[end + begin]) &&
               p[end + begin] != ':' && p[end + begin] != '(')
            ++end;

        if (end == 0 || end >= sizeof(history_command))
            continue;

        memcpy(history_command, p + begin, end);
        history_command[end] = '\0';

        if (strcmp(history_command, command) != 0)
            continue;

        p += begin + end;

        while (*p != '\0')
        {
            char option[AC_MAX_TOKEN];
            size_t n = 0;

            while (*p != '\0' && ac_is_space((unsigned char)*p))
                ++p;

            if (*p == '\0')
                break;

            while (p[n] != '\0' && !ac_is_space((unsigned char)p[n]))
            {
                if (n + 1 < sizeof(option))
                    option[n] = p[n];
                ++n;
            }

            if (n < sizeof(option) && n > 0 && option[0] == '-')
            {
                option[n] = '\0';
                if (ac_prefix_match(option, prefix))
                    ac_add_candidate(
                        list,
                        count,
                        option,
                        AC_SCORE_OPTION,
                        AC_KIND_OPTION
                    );
            }

            p += n;
        }
    }
}

static void ac_add_candidate_prefix_bonus(
    ac_candidate_t *list,
    size_t count,
    const char *prefix)
{
    size_t i;
    size_t n;

    n = strlen(prefix);

    for (i = 0; i < count; ++i)
    {
        if (ac_prefix_match(list[i].text, prefix))
            list[i].score += AC_SCORE_PREFIX + (double)n * 12.0;
    }
}

void ussr_autocomplete_init(void)
{
    memset(&state, 0, sizeof(state));
}

void ussr_autocomplete_cleanup(void)
{
    memset(&state, 0, sizeof(state));
}

void ussr_autocomplete_set_source(const char *source)
{
    if (source == NULL)
    {
        state.source[0] = '\0';
        return;
    }

    strncpy(
        state.source,
        source,
        sizeof(state.source) - 1
    );
    state.source[sizeof(state.source) - 1] = '\0';
}

void ussr_autocomplete_record_history(const char *line)
{
    unsigned i;

    if (line == NULL || line[0] == '\0')
        return;

    for (i = 0; i < state.history_count; ++i)
    {
        if (strcmp(state.history[i], line) == 0)
        {
            memmove(
                &state.history[0],
                &state.history[1],
                i * sizeof(state.history[0])
            );
            strncpy(
                state.history[0],
                line,
                sizeof(state.history[0]) - 1
            );
            state.history[0][sizeof(state.history[0]) - 1] = '\0';
            ++state.serial;
            return;
        }
    }

    if (state.history_count < USSR_AUTOCOMPLETE_MAX_HISTORY)
    {
        memmove(
            &state.history[1],
            &state.history[0],
            state.history_count * sizeof(state.history[0])
        );
        ++state.history_count;
    }
    else
    {
        memmove(
            &state.history[1],
            &state.history[0],
            (USSR_AUTOCOMPLETE_MAX_HISTORY - 1) *
                sizeof(state.history[0])
        );
    }

    strncpy(
        state.history[0],
        line,
        sizeof(state.history[0]) - 1
    );
    state.history[0][sizeof(state.history[0]) - 1] = '\0';
    ++state.serial;
}

void ussr_autocomplete_callback(
    const char *line,
    int cursor,
    bestlineCompletions *completions)
{
    ac_candidate_t candidates[USSR_AUTOCOMPLETE_MAX_CANDIDATES];
    char token[AC_MAX_TOKEN];
    size_t token_start;
    size_t count = 0;
    size_t i;
    int command_context;
    int argument_context;

    if (completions == NULL || line == NULL)
        return;

    memset(candidates, 0, sizeof(candidates));

    ac_extract_token(
        line,
        cursor,
        token,
        sizeof(token),
        &token_start
    );

    command_context = ac_in_command_head(line, token_start);
    argument_context = ac_argument_position(line, token_start, token);

    if (command_context && !argument_context)
    {
        if (token[0] == '.' || token[0] == '~' ||
            strchr(token, '/') != NULL || strchr(token, '\\') != NULL)
            ac_add_filesystem_candidates(token, candidates, &count);
        else
        {
            ac_add_commands(token, candidates, &count);
            ac_add_history_commands(token, candidates, &count);
            ac_add_source_identifiers(token, candidates, &count);
        }
    }
    else
    {
        /* In an argument position, bare names are also filesystem
         * candidates from the current directory. */
        ac_add_filesystem_candidates(token, candidates, &count);

        ac_add_history_options(line, token, candidates, &count);
        ac_add_source_identifiers(token, candidates, &count);
    }

    ac_add_candidate_prefix_bonus(candidates, count, token);
    qsort(
        candidates,
        count,
        sizeof(candidates[0]),
        ac_compare_candidates
    );

    for (i = 0; i < count; ++i)
        bestlineAddCompletion(completions, candidates[i].text);

    ac_free_candidates(candidates, count);
}
