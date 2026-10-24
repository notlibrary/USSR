#include "keybind.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int mods;
    int key;
    char *line;
} keybind_entry_t;

static keybind_entry_t *g_bindings = NULL;
static size_t g_binding_count = 0;
static size_t g_binding_capacity = 0;

/* ------------------------------------------------------------- */
/* chord names                                                      */
/* ------------------------------------------------------------- */

static int keybind_word_is(const char *word, size_t length, const char *name)
{
    size_t i;

    if (strlen(name) != length)
        return 0;
    for (i = 0; i < length; ++i)
    {
        if (tolower((unsigned char)word[i]) != name[i])
            return 0;
    }
    return 1;
}

static int keybind_modifier(const char *word, size_t length)
{
    if (keybind_word_is(word, length, "ctrl") ||
        keybind_word_is(word, length, "control") ||
        keybind_word_is(word, length, "c"))
        return USSR_KEY_CTRL;
    if (keybind_word_is(word, length, "alt") ||
        keybind_word_is(word, length, "meta") ||
        keybind_word_is(word, length, "mod") ||
        keybind_word_is(word, length, "m"))
        return USSR_KEY_ALT;
    if (keybind_word_is(word, length, "shift") ||
        keybind_word_is(word, length, "s"))
        return USSR_KEY_SHIFT;
    return 0;
}

static int keybind_key(const char *word, size_t length)
{
    if (length == 1 && isalnum((unsigned char)word[0]))
        return tolower((unsigned char)word[0]);
    if (length >= 2 && length <= 3 && (word[0] == 'f' || word[0] == 'F'))
    {
        int n = 0;
        size_t i;

        for (i = 1; i < length; ++i)
        {
            if (!isdigit((unsigned char)word[i]))
                return 0;
            n = n * 10 + (word[i] - '0');
        }
        if (n >= 1 && n <= 12)
            return USSR_KEY_F(n);
    }
    return 0;
}

int ussr_keybind_parse(const char *spec, int *mods, int *key,
                       char *error, size_t error_size)
{
    const char *p = spec;
    int found_mods = 0;
    int found_key = 0;

    if (spec == NULL || spec[0] == '\0')
    {
        snprintf(error, error_size, "empty key chord");
        return -1;
    }

    while (*p != '\0')
    {
        const char *start = p;
        size_t length;
        int modifier;

        /* A '+' or '-' right at the start of a word is the key itself
         * only in names we don't support, so treat it as a separator. */
        while (*p != '\0' && *p != '+' && *p != '-')
            ++p;
        length = (size_t)(p - start);
        if (*p != '\0')
            ++p;
        if (length == 0)
            continue;

        if (found_key != 0)
        {
            snprintf(error, error_size,
                     "'%s': the key must come last", spec);
            return -1;
        }
        modifier = keybind_modifier(start, length);
        /* "c", "m" and "s" alone are keys, not modifiers, when last. */
        if (modifier != 0 && !(length == 1 && *p == '\0'))
        {
            found_mods |= modifier;
            continue;
        }
        found_key = keybind_key(start, length);
        if (found_key == 0)
        {
            snprintf(error, error_size,
                     "'%.*s' is not a key (use a..z, 0..9, F1..F12)",
                     (int)length, start);
            return -1;
        }
    }

    if (found_key == 0)
    {
        snprintf(error, error_size, "'%s' names no key", spec);
        return -1;
    }
    if (found_key < 0x100 && found_mods == 0)
    {
        snprintf(error, error_size,
                 "'%s' would take over a typing key; add Ctrl or Alt", spec);
        return -1;
    }
    if (found_key < 0x100 && found_mods == USSR_KEY_SHIFT)
    {
        snprintf(error, error_size,
                 "'%s' is just an uppercase letter; add Ctrl or Alt", spec);
        return -1;
    }
    if (found_mods & USSR_KEY_CTRL)
    {
        if (isdigit(found_key))
        {
            snprintf(error, error_size,
                     "terminals do not send Ctrl with a digit; use Alt");
            return -1;
        }
        if (found_key == 'h' || found_key == 'i' ||
            found_key == 'j' || found_key == 'm')
        {
            snprintf(error, error_size,
                     "'%s' is the same key as %s", spec,
                     found_key == 'h' ? "Backspace" :
                     found_key == 'i' ? "Tab" : "Enter");
            return -1;
        }
        /* Ctrl+Shift+letter arrives as plain Ctrl+letter. */
        if (found_key < 0x100)
            found_mods &= ~USSR_KEY_SHIFT;
    }
    else if (found_key < 0x100 && (found_mods & USSR_KEY_SHIFT))
    {
        /* The Windows console reports Alt+Shift+x as Alt+x, and some
         * terminals send Alt+Shift+N/O/P/X as control sequences. */
        snprintf(error, error_size,
                 "'%s': Shift with a letter or digit is not reliable; "
                 "use Alt+%c", spec, found_key);
        return -1;
    }

    *mods = found_mods;
    *key = found_key;
    return 0;
}

void ussr_keybind_name(int mods, int key, char *buffer, size_t size)
{
    char key_name[16];

    if (key >= USSR_KEY_F(1))
        snprintf(key_name, sizeof(key_name), "F%d", key - USSR_KEY_F(0));
    else
        snprintf(key_name, sizeof(key_name), "%c", toupper(key));

    snprintf(buffer, size, "%s%s%s%s",
             (mods & USSR_KEY_CTRL) ? "Ctrl+" : "",
             (mods & USSR_KEY_ALT) ? "Alt+" : "",
             (mods & USSR_KEY_SHIFT) ? "Shift+" : "",
             key_name);
}

/* ------------------------------------------------------------- */
/* table                                                            */
/* ------------------------------------------------------------- */

int ussr_keybind_set(const char *spec, const char *line,
                     char *error, size_t error_size)
{
    int mods;
    int key;
    size_t i;
    char *copy = NULL;

    if (ussr_keybind_parse(spec, &mods, &key, error, error_size) != 0)
        return -1;

    if (line != NULL && line[0] != '\0')
    {
        if (strchr(line, '\n') != NULL || strchr(line, '\r') != NULL)
        {
            snprintf(error, error_size,
                     "a bound line must be a single line");
            return -1;
        }
        copy = malloc(strlen(line) + 1);
        if (copy == NULL)
        {
            snprintf(error, error_size, "out of memory");
            return -1;
        }
        strcpy(copy, line);
    }

    for (i = 0; i < g_binding_count; ++i)
    {
        if (g_bindings[i].mods == mods && g_bindings[i].key == key)
        {
            free(g_bindings[i].line);
            if (copy != NULL)
            {
                g_bindings[i].line = copy;
            }
            else
            {
                memmove(&g_bindings[i], &g_bindings[i + 1],
                        (g_binding_count - i - 1) * sizeof(*g_bindings));
                --g_binding_count;
            }
            return 0;
        }
    }

    if (copy == NULL)
        return 0; /* unbinding a chord that was never bound */

    if (g_binding_count == g_binding_capacity)
    {
        size_t capacity = g_binding_capacity ? g_binding_capacity * 2 : 8;
        keybind_entry_t *grown =
            realloc(g_bindings, capacity * sizeof(*grown));

        if (grown == NULL)
        {
            free(copy);
            snprintf(error, error_size, "out of memory");
            return -1;
        }
        g_bindings = grown;
        g_binding_capacity = capacity;
    }
    g_bindings[g_binding_count].mods = mods;
    g_bindings[g_binding_count].key = key;
    g_bindings[g_binding_count].line = copy;
    ++g_binding_count;
    return 0;
}

const char *ussr_keybind_find(int mods, int key)
{
    size_t i;

    for (i = 0; i < g_binding_count; ++i)
    {
        if (g_bindings[i].mods == mods && g_bindings[i].key == key)
            return g_bindings[i].line;
    }
    return NULL;
}

const char *ussr_keybind_at(size_t index, int *mods, int *key)
{
    if (index >= g_binding_count)
        return NULL;
    *mods = g_bindings[index].mods;
    *key = g_bindings[index].key;
    return g_bindings[index].line;
}

/* ------------------------------------------------------------- */
/* decoding key presses                                             */
/* ------------------------------------------------------------- */

/* xterm modifier parameter: 1 + (Shift 1 | Alt 2 | Ctrl 4 | Meta 8) */
static int keybind_xterm_mods(int parameter)
{
    int bits;
    int mods = 0;

    if (parameter < 2)
        return 0;
    bits = parameter - 1;
    if (bits & 1)
        mods |= USSR_KEY_SHIFT;
    if (bits & (2 | 8))
        mods |= USSR_KEY_ALT;
    if (bits & 4)
        mods |= USSR_KEY_CTRL;
    return mods;
}

/* \e[n~ function key numbers, VT220 style. */
static int keybind_tilde_key(int n)
{
    static const int numbers[12] = { 11, 12, 13, 14, 15, 17,
                                     18, 19, 20, 21, 23, 24 };
    int i;

    for (i = 0; i < 12; ++i)
    {
        if (numbers[i] == n)
            return USSR_KEY_F(i + 1);
    }
    return 0;
}

static int keybind_decode(const unsigned char *s, size_t n,
                          int *mods, int *key)
{
    if (n == 0)
        return 0;

    if (n == 1)
    {
        if (s[0] >= 1 && s[0] <= 26)
        {
            *mods = USSR_KEY_CTRL;
            *key = 'a' + s[0] - 1;
            return 1;
        }
        return 0;
    }

    if (s[0] != 033)
        return 0;

    if (n == 2)
    {
        if (islower(s[1]) || isdigit(s[1]))
        {
            *mods = USSR_KEY_ALT;
            *key = s[1];
            return 1;
        }
        if (s[1] >= 1 && s[1] <= 26)
        {
            *mods = USSR_KEY_CTRL | USSR_KEY_ALT;
            *key = 'a' + s[1] - 1;
            return 1;
        }
        return 0;
    }

    /* Alt sent as an ESC prefix in front of another key's sequence. */
    if (s[1] == 033)
    {
        if (!keybind_decode(s + 1, n - 1, mods, key))
            return 0;
        *mods |= USSR_KEY_ALT;
        return 1;
    }

    /* SS3: \eOP..\eOS, or \eO<m>P with an xterm modifier. */
    if (s[1] == 'O')
    {
        unsigned char final = s[n - 1];
        int parameter = 0;
        size_t i;

        if (final < 'P' || final > 'S')
            return 0;
        for (i = 2; i + 1 < n; ++i)
        {
            if (!isdigit(s[i]))
                return 0;
            parameter = parameter * 10 + (s[i] - '0');
        }
        *mods = keybind_xterm_mods(parameter);
        *key = USSR_KEY_F(final - 'P' + 1);
        return 1;
    }

    if (s[1] != '[')
        return 0;

    /* Linux console: \e[[A..\e[[E are F1..F5. */
    if (n == 4 && s[2] == '[' && s[3] >= 'A' && s[3] <= 'E')
    {
        *mods = 0;
        *key = USSR_KEY_F(s[3] - 'A' + 1);
        return 1;
    }

    /* CSI: \e[ n ; m final */
    {
        int params[2] = { 0, 0 };
        int count = 0;
        unsigned char final = s[n - 1];
        size_t i;

        for (i = 2; i + 1 < n; ++i)
        {
            if (isdigit(s[i]))
            {
                if (count == 0)
                    count = 1;
                params[count - 1] = params[count - 1] * 10 + (s[i] - '0');
            }
            else if (s[i] == ';' && count == 1)
                count = 2;
            else
                return 0;
        }

        if (final == '~' || final == '^' || final == '$' || final == '@')
        {
            *key = keybind_tilde_key(params[0]);
            if (*key == 0)
                return 0;
            *mods = keybind_xterm_mods(params[1]);
            /* rxvt marks Ctrl/Shift with the final byte instead. */
            if (final == '^' || final == '@')
                *mods |= USSR_KEY_CTRL;
            if (final == '$' || final == '@')
                *mods |= USSR_KEY_SHIFT;
            return 1;
        }
        if (final >= 'P' && final <= 'S' && (count == 0 || params[0] == 1))
        {
            *mods = keybind_xterm_mods(params[1]);
            *key = USSR_KEY_F(final - 'P' + 1);
            return 1;
        }
    }
    return 0;
}

const char *ussr_keybind_match_sequence(const char *seq, size_t length)
{
    int mods;
    int key;

    if (g_binding_count == 0 || seq == NULL)
        return NULL;
    if (!keybind_decode((const unsigned char *)seq, length, &mods, &key))
        return NULL;
    return ussr_keybind_find(mods, key);
}

/* Alt+letter scan codes on a US layout, as _getch() reports them. */
static int keybind_scan_letter(int code)
{
    static const char row_q[] = "qwertyuiop";
    static const char row_a[] = "asdfghjkl";
    static const char row_z[] = "zxcvbnm";

    if (code >= 16 && code <= 25)
        return row_q[code - 16];
    if (code >= 30 && code <= 38)
        return row_a[code - 30];
    if (code >= 44 && code <= 50)
        return row_z[code - 44];
    return 0;
}

/* Chord of one extended _getch() key (after its 0x00/0xE0 prefix). */
static int keybind_getch_decode(int prefix, int code, int *mods_out,
                                int *key_out)
{
    int mods = 0;
    int key = 0;

    if (code >= 59 && code <= 68)
        key = USSR_KEY_F(code - 58);
    else if (code >= 84 && code <= 113)
    {
        static const int group_mods[3] = {
            USSR_KEY_SHIFT, USSR_KEY_CTRL, USSR_KEY_ALT
        };
        mods = group_mods[(code - 84) / 10];
        key = USSR_KEY_F((code - 84) % 10 + 1);
    }
    else if (code >= 133 && code <= 140)
    {
        static const int group_mods[4] = {
            0, USSR_KEY_SHIFT, USSR_KEY_CTRL, USSR_KEY_ALT
        };
        mods = group_mods[(code - 133) / 2];
        key = USSR_KEY_F(11 + (code - 133) % 2);
    }
    else if (code >= 120 && code <= 129)
    {
        mods = USSR_KEY_ALT;
        key = code == 129 ? '0' : '1' + (code - 120);
    }
    else if (prefix == 0x00 && keybind_scan_letter(code) != 0)
    {
        mods = USSR_KEY_ALT;
        key = keybind_scan_letter(code);
    }
    else
        return 0;

    *mods_out = mods;
    *key_out = key;
    return 1;
}

const char *ussr_keybind_match_getch(int prefix, int code)
{
    int mods;
    int key;

    if (g_binding_count == 0)
        return NULL;

    if (prefix != 0x00 && prefix != 0xE0)
    {
        /* plain character: only Ctrl+letter can be bound */
        if (code < 1 || code > 26)
            return NULL;
        return ussr_keybind_find(USSR_KEY_CTRL, 'a' + code - 1);
    }
    if (!keybind_getch_decode(prefix, code, &mods, &key))
        return NULL;
    return ussr_keybind_find(mods, key);
}

int ussr_keybind_getch_sequence(int prefix, int code,
                                char *out, size_t size)
{
    static const int tilde[12] = { 11, 12, 13, 14, 15, 17,
                                   18, 19, 20, 21, 23, 24 };
    int mods;
    int key;
    int parameter;
    int n;

    if (!keybind_getch_decode(prefix, code, &mods, &key))
        return 0;

    if (key < USSR_KEY_F(1))
        n = snprintf(out, size, "\033%c", key); /* only Alt+key decodes */
    else
    {
        int f = key - USSR_KEY_F(0);

        /* xterm: 1 + Shift 1 | Alt 2 | Ctrl 4 */
        parameter = 1 + ((mods & USSR_KEY_SHIFT) ? 1 : 0) +
                    ((mods & USSR_KEY_ALT) ? 2 : 0) +
                    ((mods & USSR_KEY_CTRL) ? 4 : 0);
        if (f <= 4 && parameter == 1)
            n = snprintf(out, size, "\033O%c", 'P' + f - 1);
        else if (f <= 4)
            n = snprintf(out, size, "\033[1;%d%c", parameter, 'P' + f - 1);
        else if (parameter == 1)
            n = snprintf(out, size, "\033[%d~", tilde[f - 1]);
        else
            n = snprintf(out, size, "\033[%d;%d~", tilde[f - 1], parameter);
    }
    return n > 0 && (size_t)n < size ? n : 0;
}
