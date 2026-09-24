#include "worstline.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <conio.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Deliberately primitive, as asked for: single logical row per line
 * (no handling of the line wrapping past the console's width), no
 * tab completion, no reverse search, no hints. Just enough editing
 * for an interactive shell prompt: insert/backspace/delete, left,
 * right, home, end, and up/down through history.
 *
 * Redraw strategy: capture the console cursor position right after
 * printing the prompt (the "origin"), and on every edit reposition to
 * `origin` with SetConsoleCursorPosition and rewrite the line, rather
 * than relying on ANSI escape sequences -- this works on any Windows
 * console back to XP, not just ones with ENABLE_VIRTUAL_TERMINAL_
 * PROCESSING turned on.
 */

/* ------------------------------------------------------------- */
/* history                                                          */
/* ------------------------------------------------------------- */

static char **g_history = NULL;
static size_t g_history_count = 0;
static size_t g_history_capacity = 0;

static worstlineCompletionCallback *g_completion_callback = NULL;

void worstlineSetCompletionCallback(
    worstlineCompletionCallback *callback
)
{
    g_completion_callback = callback;
}

void worstlineHistoryAdd(const char *line)
{
    char *copy;

    if (line == NULL || line[0] == '\0')
        return;

    /* Skip an exact repeat of the most recent entry, matching common
     * shell history behavior. */
    if (g_history_count > 0 &&
        strcmp(g_history[g_history_count - 1], line) == 0)
        return;

    copy = _strdup(line);
    if (copy == NULL)
        return;

    if (g_history_count == g_history_capacity)
    {
        size_t new_capacity = g_history_capacity ? g_history_capacity * 2 : 64;
        char **grown = realloc(g_history, new_capacity * sizeof(*grown));

        if (grown == NULL)
        {
            free(copy);
            return;
        }

        g_history = grown;
        g_history_capacity = new_capacity;
    }

    g_history[g_history_count++] = copy;
}

void worstlineFree(char *line)
{
    free(line);
}

/* ------------------------------------------------------------- */
/* line buffer + rendering                                          */
/* ------------------------------------------------------------- */

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
    size_t cursor;
    size_t rendered_length; /* how much of the row we last painted, for erasing leftovers */
    COORD origin;
    HANDLE console_out;
} ussr_line_state_t;

static int line_ensure_capacity(ussr_line_state_t *state, size_t needed)
{
    if (needed <= state->capacity)
        return 0;

    {
        size_t new_capacity = state->capacity ? state->capacity * 2 : 128;
        char *grown;

        while (new_capacity < needed)
            new_capacity *= 2;

        grown = realloc(state->data, new_capacity);
        if (grown == NULL)
            return -1;

        state->data = grown;
        state->capacity = new_capacity;
    }

    return 0;
}

static void line_set_cursor_pos(ussr_line_state_t *state, size_t offset)
{
    COORD pos = state->origin;
    pos.X = (SHORT)(state->origin.X + offset);
    SetConsoleCursorPosition(state->console_out, pos);
}

static void line_redraw(ussr_line_state_t *state)
{
    DWORD written;

    SetConsoleCursorPosition(state->console_out, state->origin);

    if (state->length > 0)
        WriteConsoleA(state->console_out, state->data, (DWORD)state->length, &written, NULL);

    if (state->rendered_length > state->length)
    {
        size_t pad = state->rendered_length - state->length;
        COORD erase_pos = state->origin;
        erase_pos.X = (SHORT)(state->origin.X + state->length);

        FillConsoleOutputCharacterA(
            state->console_out, ' ', (DWORD)pad,
            erase_pos,
            &written
        );
    }

    state->rendered_length = state->length;

    line_set_cursor_pos(state, state->cursor);
}

static int line_set_text(ussr_line_state_t *state, const char *text)
{
    size_t text_length = strlen(text);

    if (line_ensure_capacity(state, text_length + 1) != 0)
        return -1;

    memcpy(state->data, text, text_length + 1);
    state->length = text_length;
    state->cursor = text_length;

    line_redraw(state);
    return 0;
}

/* ------------------------------------------------------------- */
/* main entry point                                                 */
/* ------------------------------------------------------------- */

char *worstline(const char *prompt)
{
    ussr_line_state_t state;
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    size_t history_index; /* g_history_count means "not browsing, on the live line" */
    char *saved_line = NULL;
    char *result;

    memset(&state, 0, sizeof(state));
    state.console_out = GetStdHandle(STD_OUTPUT_HANDLE);

    if (prompt != NULL)
        fputs(prompt, stdout);
    fflush(stdout);

    GetConsoleScreenBufferInfo(state.console_out, &csbi);
    state.origin = csbi.dwCursorPosition;

    if (line_ensure_capacity(&state, 128) != 0)
        return NULL;
    state.data[0] = '\0';

    history_index = g_history_count;

    for (;;)
    {
        int c = _getch();

        if (c == '\t')
        {
            char **matches = NULL;
            size_t match_count = 0;

            if (g_completion_callback != NULL)
                g_completion_callback(state.data, state.cursor,
                                      &matches, &match_count);

            if (match_count > 0 && matches != NULL)
                line_set_text(&state, matches[0]);

            if (matches != NULL)
            {
                size_t i;
                for (i = 0; i < match_count; ++i)
                    free(matches[i]);
                free(matches);
            }
            continue;
        }

        if (c == '\r' || c == '\n')
        {
            putchar('\n');
            break;
        }

        if (c == 0x1A) /* Ctrl-Z: treat as EOF, like Ctrl-D on POSIX */
        {
            putchar('\n');
            free(state.data);
            free(saved_line);
            return NULL;
        }

        if (c == 0x04) /* Ctrl-D */
        {
            if (state.length == 0)
            {
                putchar('\n');
                free(state.data);
                free(saved_line);
                return NULL;
            }

            if (state.cursor < state.length)
            {
                memmove(
                    state.data + state.cursor,
                    state.data + state.cursor + 1,
                    state.length - state.cursor - 1
                );
                state.length--;
                state.data[state.length] = '\0';
                line_redraw(&state);
            }
            continue;
        }

        if (c == '\b' || c == 127) /* Backspace */
        {
            if (state.cursor > 0)
            {
                memmove(
                    state.data + state.cursor - 1,
                    state.data + state.cursor,
                    state.length - state.cursor
                );
                state.cursor--;
                state.length--;
                state.data[state.length] = '\0';
                line_redraw(&state);
            }
            continue;
        }

        if (c == 0 || c == 0xE0) /* extended-key prefix */
        {
            int c2 = _getch();

            switch (c2)
            {
            case 75: /* Left */
                if (state.cursor > 0)
                {
                    state.cursor--;
                    line_set_cursor_pos(&state, state.cursor);
                }
                break;

            case 77: /* Right */
                if (state.cursor < state.length)
                {
                    state.cursor++;
                    line_set_cursor_pos(&state, state.cursor);
                }
                break;

            case 71: /* Home */
                state.cursor = 0;
                line_set_cursor_pos(&state, state.cursor);
                break;

            case 79: /* End */
                state.cursor = state.length;
                line_set_cursor_pos(&state, state.cursor);
                break;

            case 83: /* Delete */
                if (state.cursor < state.length)
                {
                    memmove(
                        state.data + state.cursor,
                        state.data + state.cursor + 1,
                        state.length - state.cursor - 1
                    );
                    state.length--;
                    state.data[state.length] = '\0';
                    line_redraw(&state);
                }
                break;

            case 72: /* Up: older history */
                if (history_index > 0)
                {
                    if (history_index == g_history_count)
                    {
                        free(saved_line);
                        saved_line = _strdup(state.data);
                    }
                    history_index--;
                    line_set_text(&state, g_history[history_index]);
                }
                break;

            case 80: /* Down: newer history, or back to the live line */
                if (history_index < g_history_count)
                {
                    history_index++;
                    if (history_index == g_history_count)
                        line_set_text(&state, saved_line != NULL ? saved_line : "");
                    else
                        line_set_text(&state, g_history[history_index]);
                }
                break;

            default:
                break;
            }
            continue;
        }

        if (c >= 32 && c < 127) /* printable */
        {
            if (line_ensure_capacity(&state, state.length + 2) != 0)
                continue;

            memmove(
                state.data + state.cursor + 1,
                state.data + state.cursor,
                state.length - state.cursor
            );
            state.data[state.cursor] = (char)c;
            state.cursor++;
            state.length++;
            state.data[state.length] = '\0';

            line_redraw(&state);
            continue;
        }

        /* anything else (other control characters) is ignored */
    }

    result = _strdup(state.data);

    free(state.data);
    free(saved_line);

    return result;
}
