/*
 * regex.c - the regular expression engine: see regex.h. The pattern is read by
 * recursive descent and turned straight into instructions with relative jumps,
 * so a piece of code can be moved or copied, which is how repeats are made.
 * Text is UTF-8: . and classes take whole characters, offsets are in bytes, and
 * a byte that is not valid UTF-8 is a character of its own.
 */
#include "regex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RE_MAX_CODE 4096
#define RE_MAX_RANGES 4096
#define RE_MAX_REPEAT 1000
#define RE_MAX_DEPTH 100
#define RE_MAX_GROUPS 32
#define RE_MAX_CHAR 0x10ffff
#define RE_BAD_BYTE 0x110000 /* + the byte: not valid UTF-8 */

enum
{
    OP_CHAR,   /* x: the character */
    OP_CHARI,  /* x: the character, lower case */
    OP_ANY,    /* any but a newline */
    OP_ANYNL,  /* any */
    OP_CLASS,  /* x: first range, y: ranges, z: negated */
    OP_CLASSI, /* the same, either case */
    OP_SPLIT,  /* to +x first, then +y */
    OP_JMP,    /* to +x */
    OP_SAVE,   /* x: the slot */
    OP_BOL,    /* start of text */
    OP_EOL,    /* end of text */
    OP_MBOL,   /* start of a line */
    OP_MEOL,   /* end of a line */
    OP_WORDB,  /* at a word boundary */
    OP_NWORDB, /* not at one */
    OP_MATCH
};

typedef struct
{
    int op;
    int x;
    int y;
    int z;
} re_inst_t;

struct ussr_regex_t
{
    re_inst_t code[RE_MAX_CODE];
    int code_length;
    int ranges[RE_MAX_RANGES * 2];
    int range_count;
    int groups;
    int slots;
    /* the search's working memory: made once, used by every search */
    long *threads[2];
    int counts[2];
    long *saves;
    long *stack;
    unsigned *marks;
    unsigned generation;
};

typedef struct
{
    ussr_regex_t *re;
    const unsigned char *p;
    int depth;
    int icase;
    int multi;
    int dotall;
    const char *error;
} re_parser_t;

static const int re_digit[] = { '0', '9' };
static const int re_not_digit[] = { 0, '0' - 1, '9' + 1, RE_BAD_BYTE + 255 };
static const int re_word[] = { '0', '9', 'A', 'Z', '_', '_', 'a', 'z' };
static const int re_not_word[] = { 0, '0' - 1, '9' + 1, 'A' - 1, 'Z' + 1, '_' - 1,
                                   '_' + 1, 'a' - 1, 'z' + 1, RE_BAD_BYTE + 255 };
static const int re_space[] = { 9, 13, ' ', ' ' };
static const int re_not_space[] = { 0, 8, 14, ' ' - 1, ' ' + 1, RE_BAD_BYTE + 255 };

/* a character of text at p, end its end -> the character; *size its bytes */
static int re_decode(const unsigned char *p, const unsigned char *end, size_t *size)
{
    int c = p[0];
    int need, i, value;

    *size = 1;
    if (c < 0x80)
        return c;
    if (c >= 0xc2 && c <= 0xdf)
    {
        need = 1;
        value = c & 0x1f;
    }
    else if (c >= 0xe0 && c <= 0xef)
    {
        need = 2;
        value = c & 0x0f;
    }
    else if (c >= 0xf0 && c <= 0xf4)
    {
        need = 3;
        value = c & 0x07;
    }
    else
        return RE_BAD_BYTE + c;
    if (end - p <= need)
        return RE_BAD_BYTE + c;
    for (i = 1; i <= need; i++)
    {
        if ((p[i] & 0xc0) != 0x80)
            return RE_BAD_BYTE + c;
        value = (value << 6) | (p[i] & 0x3f);
    }
    if ((need == 2 && value < 0x800) || (need == 3 && (value < 0x10000 || value > RE_MAX_CHAR)))
        return RE_BAD_BYTE + c;
    *size = (size_t)need + 1;
    return value;
}

size_t ussr_regex_char_length(const char *text, size_t length, size_t at)
{
    size_t size = 1;

    if (at < length)
        re_decode((const unsigned char *)text + at, (const unsigned char *)text + length, &size);
    return size;
}

static int re_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

static int re_is_word(int c)
{
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

/* --- making code */

static int re_emit(re_parser_t *ps, int op, int x, int y, int z)
{
    ussr_regex_t *re = ps->re;

    if (re->code_length >= RE_MAX_CODE)
    {
        ps->error = "too big";
        return -1;
    }
    re->code[re->code_length].op = op;
    re->code[re->code_length].x = x;
    re->code[re->code_length].y = y;
    re->code[re->code_length].z = z;
    return re->code_length++;
}

/* room for one instruction at at: the code after it moves up one. Its jumps
 * are relative and stay inside it, so it runs as before. */
static int re_insert(re_parser_t *ps, int at, int op, int x, int y)
{
    ussr_regex_t *re = ps->re;

    if (re->code_length >= RE_MAX_CODE)
    {
        ps->error = "too big";
        return -1;
    }
    memmove(&re->code[at + 1], &re->code[at], (size_t)(re->code_length - at) * sizeof re->code[0]);
    re->code_length++;
    re->code[at].op = op;
    re->code[at].x = x;
    re->code[at].y = y;
    re->code[at].z = 0;
    return at;
}

/* the code of [from, to) again, at the end */
static int re_copy(re_parser_t *ps, int from, int to)
{
    ussr_regex_t *re = ps->re;
    int n = to - from;

    if (re->code_length + n > RE_MAX_CODE)
    {
        ps->error = "too big";
        return -1;
    }
    memcpy(&re->code[re->code_length], &re->code[from], (size_t)n * sizeof re->code[0]);
    re->code_length += n;
    return 0;
}

static int re_add_range(re_parser_t *ps, int low, int high)
{
    ussr_regex_t *re = ps->re;

    if (re->range_count >= RE_MAX_RANGES)
    {
        ps->error = "too big";
        return -1;
    }
    re->ranges[re->range_count * 2] = low;
    re->ranges[re->range_count * 2 + 1] = high;
    re->range_count++;
    return 0;
}

static int re_add_ranges(re_parser_t *ps, const int *pairs, size_t numbers)
{
    size_t i;

    for (i = 0; i < numbers; i += 2)
        if (re_add_range(ps, pairs[i], pairs[i + 1]) != 0)
            return -1;
    return 0;
}

/* a shorthand letter's ranges into the class being made: 1 when it was one */
static int re_shorthand(re_parser_t *ps, int letter)
{
#define RE_RANGES(a) re_add_ranges(ps, a, sizeof a / sizeof a[0])
    switch (letter)
    {
    case 'd': RE_RANGES(re_digit); return 1;
    case 'D': RE_RANGES(re_not_digit); return 1;
    case 'w': RE_RANGES(re_word); return 1;
    case 'W': RE_RANGES(re_not_word); return 1;
    case 's': RE_RANGES(re_space); return 1;
    case 'S': RE_RANGES(re_not_space); return 1;
    default: return 0;
    }
#undef RE_RANGES
}

static int re_hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* the character after a backslash, p at it; -1 with the error set */
static int re_escape(re_parser_t *ps)
{
    int c = *ps->p;
    size_t size;

    if (c == 0)
    {
        ps->error = "\\ at the end";
        return -1;
    }
    if (c >= '1' && c <= '9')
    {
        ps->error = "backreferences are not supported";
        return -1;
    }
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '0')
    {
        ps->p++;
        switch (c)
        {
        case 'n': return 10;
        case 't': return 9;
        case 'r': return 13;
        case 'f': return 12;
        case 'v': return 11;
        case 'x':
        {
            int high = re_hex(ps->p[0]);
            int low = high < 0 ? -1 : re_hex(ps->p[1]);

            if (low < 0)
            {
                ps->error = "\\x takes two hex digits";
                return -1;
            }
            ps->p += 2;
            return high * 16 + low;
        }
        default:
            ps->error = "unknown escape";
            return -1;
        }
    }
    c = re_decode(ps->p, ps->p + strlen((const char *)ps->p), &size);
    ps->p += size;
    return c;
}

/* [...], p after the [ */
static int re_parse_class(re_parser_t *ps)
{
    ussr_regex_t *re = ps->re;
    int first = re->range_count;
    int negated = 0;
    int any = 0;

    if (*ps->p == '^')
    {
        negated = 1;
        ps->p++;
    }
    for (;;)
    {
        int low;
        size_t size;

        if (*ps->p == 0)
        {
            ps->error = "missing ]";
            return -1;
        }
        if (*ps->p == ']' && any)
        {
            ps->p++;
            break;
        }
        any = 1;
        if (*ps->p == '\\')
        {
            ps->p++;
            if (re_shorthand(ps, *ps->p))
            {
                if (ps->error != NULL)
                    return -1;
                ps->p++;
                continue;
            }
            if (*ps->p == 'b')
            {
                ps->p++;
                low = 8;
            }
            else
            {
                low = re_escape(ps);
                if (low < 0)
                    return -1;
            }
        }
        else
        {
            low = re_decode(ps->p, ps->p + strlen((const char *)ps->p), &size);
            ps->p += size;
        }
        if (ps->p[0] == '-' && ps->p[1] != ']' && ps->p[1] != 0)
        {
            int high;

            ps->p++;
            if (*ps->p == '\\')
            {
                ps->p++;
                high = re_escape(ps);
                if (high < 0)
                    return -1;
            }
            else
            {
                high = re_decode(ps->p, ps->p + strlen((const char *)ps->p), &size);
                ps->p += size;
            }
            if (high < low)
            {
                ps->error = "bad range";
                return -1;
            }
            if (re_add_range(ps, low, high) != 0)
                return -1;
        }
        else if (re_add_range(ps, low, low) != 0)
            return -1;
    }
    return re_emit(ps, ps->icase ? OP_CLASSI : OP_CLASS, first, re->range_count - first, negated) < 0 ? -1 : 0;
}

static int re_parse_alternatives(re_parser_t *ps);

/* one atom's code */
static int re_parse_atom(re_parser_t *ps)
{
    int c = *ps->p;
    size_t size;

    switch (c)
    {
    case '(':
    {
        int group = 0;

        ps->p++;
        if (*ps->p == '?')
        {
            if (ps->p[1] != ':')
            {
                ps->error = "of (? only (?: is supported";
                return -1;
            }
            ps->p += 2;
            group = -1;
        }
        if (group == 0)
        {
            if (ps->re->groups >= RE_MAX_GROUPS)
            {
                ps->error = "too many groups";
                return -1;
            }
            group = ++ps->re->groups;
            if (re_emit(ps, OP_SAVE, group * 2, 0, 0) < 0)
                return -1;
        }
        if (++ps->depth > RE_MAX_DEPTH)
        {
            ps->error = "too big";
            return -1;
        }
        if (re_parse_alternatives(ps) != 0)
            return -1;
        ps->depth--;
        if (*ps->p != ')')
        {
            ps->error = "missing )";
            return -1;
        }
        ps->p++;
        if (group > 0 && re_emit(ps, OP_SAVE, group * 2 + 1, 0, 0) < 0)
            return -1;
        return 0;
    }
    case '[':
        ps->p++;
        return re_parse_class(ps);
    case '.':
        ps->p++;
        return re_emit(ps, ps->dotall ? OP_ANYNL : OP_ANY, 0, 0, 0) < 0 ? -1 : 0;
    case '^':
        ps->p++;
        return re_emit(ps, ps->multi ? OP_MBOL : OP_BOL, 0, 0, 0) < 0 ? -1 : 0;
    case '$':
        ps->p++;
        return re_emit(ps, ps->multi ? OP_MEOL : OP_EOL, 0, 0, 0) < 0 ? -1 : 0;
    case '*':
    case '+':
    case '?':
        ps->error = "nothing to repeat";
        return -1;
    case '\\':
        ps->p++;
        if (*ps->p == 'b' || *ps->p == 'B')
        {
            int op = *ps->p == 'b' ? OP_WORDB : OP_NWORDB;

            ps->p++;
            return re_emit(ps, op, 0, 0, 0) < 0 ? -1 : 0;
        }
        if (*ps->p == 'd' || *ps->p == 'D' || *ps->p == 'w' || *ps->p == 'W' || *ps->p == 's' || *ps->p == 'S')
        {
            int first = ps->re->range_count;

            re_shorthand(ps, *ps->p);
            if (ps->error != NULL)
                return -1;
            ps->p++;
            /* no case to fold in these */
            return re_emit(ps, OP_CLASS, first, ps->re->range_count - first, 0) < 0 ? -1 : 0;
        }
        c = re_escape(ps);
        if (c < 0)
            return -1;
        break;
    default:
        c = re_decode(ps->p, ps->p + strlen((const char *)ps->p), &size);
        ps->p += size;
        break;
    }
    if (ps->icase)
        return re_emit(ps, OP_CHARI, re_lower(c), 0, 0) < 0 ? -1 : 0;
    return re_emit(ps, OP_CHAR, c, 0, 0) < 0 ? -1 : 0;
}

/* {n}, {n,} or {n,m} at p -> 1 with them read, 0 when it is not one; m is -1 for
 * no limit */
static int re_parse_counts(re_parser_t *ps, int *n, int *m)
{
    const unsigned char *q = ps->p + 1;
    long low = 0, high = -1;

    if (*q < '0' || *q > '9')
        return 0;
    while (*q >= '0' && *q <= '9')
    {
        low = low * 10 + (*q++ - '0');
        if (low > RE_MAX_REPEAT)
        {
            ps->error = "bad {n,m}";
            return 1;
        }
    }
    if (*q == '}')
        high = low;
    else if (*q == ',')
    {
        q++;
        if (*q != '}')
        {
            if (*q < '0' || *q > '9')
                return 0;
            high = 0;
            while (*q >= '0' && *q <= '9')
            {
                high = high * 10 + (*q++ - '0');
                if (high > RE_MAX_REPEAT)
                {
                    ps->error = "bad {n,m}";
                    return 1;
                }
            }
            if (high < low)
            {
                ps->error = "bad {n,m}";
                return 1;
            }
        }
        if (*q != '}')
            return 0;
    }
    else
        return 0;
    ps->p = q + 1;
    *n = (int)low;
    *m = (int)high;
    return 1;
}

/* the atom at [start, end of code) repeated: a*, a+, a? and a{n,m} */
static int re_repeat(re_parser_t *ps, int start, int n, int m, int lazy)
{
    ussr_regex_t *re = ps->re;
    int size = re->code_length - start;
    int i;

    if (n == 0 && m == 0)
    {
        re->code_length = start;
        return 0;
    }
    /* n copies, the last of them looping when there is no limit */
    for (i = 1; i < n; i++)
        if (re_copy(ps, start, start + size) != 0)
            return -1;
    if (m < 0)
    {
        if (n == 0)
        {
            /* L1: split L2 L3; L2: atom; jmp L1; L3: */
            if (re_insert(ps, start, OP_SPLIT, lazy ? size + 2 : 1, lazy ? 1 : size + 2) < 0)
                return -1;
            if (re_emit(ps, OP_JMP, -(size + 1), 0, 0) < 0)
                return -1;
        }
        else
        {
            /* atom; split back, on */
            if (re_emit(ps, OP_SPLIT, lazy ? 1 : -size, lazy ? -size : 1, 0) < 0)
                return -1;
        }
        return 0;
    }
    /* m - n optional ones: each can skip all that are left */
    if (n == 0)
    {
        int left = m - 1;
        int total = (left + 1) * (size + 1);

        if (total > RE_MAX_CODE)
        {
            ps->error = "too big";
            return -1;
        }
        if (re_insert(ps, start, OP_SPLIT, lazy ? total : 1, lazy ? 1 : total) < 0)
            return -1;
        for (i = 0; i < left; i++)
        {
            int to_end = (left - i) * (size + 1);

            if (re_emit(ps, OP_SPLIT, lazy ? to_end : 1, lazy ? 1 : to_end, 0) < 0)
                return -1;
            if (re_copy(ps, start + 1, start + 1 + size) != 0)
                return -1;
        }
        return 0;
    }
    for (i = n; i < m; i++)
    {
        int to_end = (m - i) * (size + 1);

        if (re_emit(ps, OP_SPLIT, lazy ? to_end : 1, lazy ? 1 : to_end, 0) < 0)
            return -1;
        if (re_copy(ps, start, start + size) != 0)
            return -1;
    }
    return 0;
}

/* an atom and what repeats it */
static int re_parse_piece(re_parser_t *ps)
{
    int start = ps->re->code_length;
    int n, m, lazy = 0;

    if (re_parse_atom(ps) != 0)
        return -1;
    if (*ps->p == '*')
    {
        n = 0;
        m = -1;
        ps->p++;
    }
    else if (*ps->p == '+')
    {
        n = 1;
        m = -1;
        ps->p++;
    }
    else if (*ps->p == '?')
    {
        n = 0;
        m = 1;
        ps->p++;
    }
    else if (*ps->p == '{')
    {
        if (!re_parse_counts(ps, &n, &m))
            return 0; /* a { of its own */
        if (ps->error != NULL)
            return -1;
    }
    else
        return 0;
    if (*ps->p == '?')
    {
        lazy = 1;
        ps->p++;
    }
    if (*ps->p == '*' || *ps->p == '+' || *ps->p == '?')
    {
        ps->error = "nothing to repeat";
        return -1;
    }
    if (*ps->p == '{')
    {
        int a, b;
        const unsigned char *keep = ps->p;

        if (re_parse_counts(ps, &a, &b))
        {
            ps->p = keep;
            ps->error = "nothing to repeat";
            return -1;
        }
    }
    return re_repeat(ps, start, n, m, lazy);
}

/* a | b | c */
static int re_parse_alternatives(re_parser_t *ps)
{
    ussr_regex_t *re = ps->re;
    int start = re->code_length;

    while (*ps->p != 0 && *ps->p != '|' && *ps->p != ')')
        if (re_parse_piece(ps) != 0)
            return -1;
    if (*ps->p == '|')
    {
        int size, jump;

        ps->p++;
        size = re->code_length - start;
        /* split A B; A: ...; jmp end; B: ... */
        if (re_insert(ps, start, OP_SPLIT, 1, size + 2) < 0)
            return -1;
        jump = re_emit(ps, OP_JMP, 0, 0, 0);
        if (jump < 0)
            return -1;
        if (re_parse_alternatives(ps) != 0)
            return -1;
        re->code[jump].x = re->code_length - jump;
    }
    return 0;
}

ussr_regex_t *ussr_regex_compile(const char *pattern, const char *flags,
                                 char *error, size_t error_size)
{
    re_parser_t ps;
    ussr_regex_t *re;
    size_t words;

    memset(&ps, 0, sizeof ps);
    for (; flags != NULL && *flags != 0; flags++)
    {
        if (*flags == 'i')
            ps.icase = 1;
        else if (*flags == 'm')
            ps.multi = 1;
        else if (*flags == 's')
            ps.dotall = 1;
        else
        {
            snprintf(error, error_size, "the flags are i, m and s");
            return NULL;
        }
    }
    re = (ussr_regex_t *)calloc(1, sizeof *re);
    if (re == NULL)
    {
        snprintf(error, error_size, "out of memory");
        return NULL;
    }
    ps.re = re;
    ps.p = (const unsigned char *)pattern;
    if (re_parse_alternatives(&ps) != 0 ||
        (*ps.p == ')' && (ps.error = "unmatched )") != NULL) ||
        re_emit(&ps, OP_MATCH, 0, 0, 0) < 0)
    {
        snprintf(error, error_size, "%s", ps.error);
        free(re);
        return NULL;
    }
    re->slots = (re->groups + 1) * 2;
    words = (size_t)re->code_length * (size_t)(re->slots + 1);
    re->threads[0] = (long *)malloc(words * sizeof(long));
    re->threads[1] = (long *)malloc(words * sizeof(long));
    re->saves = (long *)malloc((size_t)re->slots * sizeof(long));
    re->stack = (long *)malloc(((size_t)re->code_length * 2 + 2) * 3 * sizeof(long));
    re->marks = (unsigned *)calloc((size_t)re->code_length, sizeof(unsigned));
    if (re->threads[0] == NULL || re->threads[1] == NULL || re->saves == NULL ||
        re->stack == NULL || re->marks == NULL)
    {
        ussr_regex_free(re);
        snprintf(error, error_size, "out of memory");
        return NULL;
    }
    return re;
}

void ussr_regex_free(ussr_regex_t *re)
{
    if (re == NULL)
        return;
    free(re->threads[0]);
    free(re->threads[1]);
    free(re->saves);
    free(re->stack);
    free(re->marks);
    free(re);
}

int ussr_regex_groups(const ussr_regex_t *re)
{
    return re->groups;
}

/* --- running */

static int re_word_at(const unsigned char *text, size_t length, size_t at)
{
    return at < length && re_is_word(text[at]);
}

/* pc and all it leads to without taking a character, in priority order, into
 * list, at text position at. re->saves are the slots so far. */
static void re_add_thread(ussr_regex_t *re, int list, int pc0, const unsigned char *text,
                          size_t length, size_t at)
{
    long *stack = re->stack;
    int top = 0;

    stack[0] = pc0;
    stack[1] = -1;
    stack[2] = 0;
    top = 1;
    while (top > 0)
    {
        long *e;
        int pc;
        const re_inst_t *in;
        int before, after;

        top--;
        e = stack + top * 3;
        if (e[1] >= 0)
        {
            re->saves[e[1]] = e[2];
            continue;
        }
        pc = (int)e[0];
        if (re->marks[pc] == re->generation)
            continue;
        re->marks[pc] = re->generation;
        in = &re->code[pc];
        switch (in->op)
        {
        case OP_JMP:
            e[0] = pc + in->x;
            top++;
            break;
        case OP_SPLIT:
            /* the second way waits under the first */
            e[0] = pc + in->y;
            e[3] = pc + in->x;
            e[4] = -1;
            e[5] = 0;
            top += 2;
            break;
        case OP_SAVE:
            /* put back what it was when the ways after this one are done */
            e[0] = 0;
            e[1] = in->x;
            e[2] = re->saves[in->x];
            e[3] = pc + 1;
            e[4] = -1;
            e[5] = 0;
            top += 2;
            re->saves[in->x] = (long)at;
            break;
        case OP_BOL:
            if (at == 0)
            {
                e[0] = pc + 1;
                top++;
            }
            break;
        case OP_EOL:
            if (at == length)
            {
                e[0] = pc + 1;
                top++;
            }
            break;
        case OP_MBOL:
            if (at == 0 || text[at - 1] == 10)
            {
                e[0] = pc + 1;
                top++;
            }
            break;
        case OP_MEOL:
            if (at == length || text[at] == 10)
            {
                e[0] = pc + 1;
                top++;
            }
            break;
        case OP_WORDB:
        case OP_NWORDB:
            before = at > 0 && re_is_word(text[at - 1]);
            after = re_word_at(text, length, at);
            if ((before != after) == (in->op == OP_WORDB))
            {
                e[0] = pc + 1;
                top++;
            }
            break;
        default:
        {
            long *t = re->threads[list] + (size_t)re->counts[list] * (size_t)(re->slots + 1);

            t[0] = pc;
            memcpy(t + 1, re->saves, (size_t)re->slots * sizeof(long));
            re->counts[list]++;
            break;
        }
        }
    }
}

static int re_class_has(const ussr_regex_t *re, const re_inst_t *in, int c)
{
    int i;

    for (i = 0; i < in->y; i++)
        if (c >= re->ranges[(in->x + i) * 2] && c <= re->ranges[(in->x + i) * 2 + 1])
            return 1;
    return 0;
}

int ussr_regex_search(ussr_regex_t *re, const char *text_chars, size_t length,
                      size_t from, long *spans)
{
    const unsigned char *text = (const unsigned char *)text_chars;
    int list = 0;
    int found = 0;
    size_t at = from;
    int i;

    re->counts[0] = re->counts[1] = 0;
    if (from > length)
        return 0;
    re->generation++;
    for (;;)
    {
        int c = -1;
        size_t size = 1;
        int next = 1 - list;
        int t;

        if (!found)
        {
            /* a match could start here, after all that started before it */
            for (i = 0; i < re->slots; i++)
                re->saves[i] = -1;
            re->saves[0] = (long)at;
            re_add_thread(re, list, 0, text, length, at);
        }
        if (re->counts[list] == 0 && (found || at >= length))
            break;
        if (at < length)
            c = re_decode(text + at, text + length, &size);
        re->generation++;
        re->counts[next] = 0;
        for (t = 0; t < re->counts[list]; t++)
        {
            long *th = re->threads[list] + (size_t)t * (size_t)(re->slots + 1);
            int pc = (int)th[0];
            const re_inst_t *in = &re->code[pc];
            int take = 0;

            switch (in->op)
            {
            case OP_MATCH:
                memcpy(spans, th + 1, (size_t)re->slots * sizeof(long));
                spans[1] = (long)at;
                found = 1;
                t = re->counts[list]; /* those after it have lower priority */
                continue;
            case OP_CHAR:
                take = c == in->x;
                break;
            case OP_CHARI:
                take = c >= 0 && re_lower(c) == in->x;
                break;
            case OP_ANY:
                take = c >= 0 && c != 10;
                break;
            case OP_ANYNL:
                take = c >= 0;
                break;
            case OP_CLASS:
                take = c >= 0 && re_class_has(re, in, c) != in->z;
                break;
            case OP_CLASSI:
                if (c >= 0)
                {
                    int has = re_class_has(re, in, c);

                    if (!has && c >= 'a' && c <= 'z')
                        has = re_class_has(re, in, c - 32);
                    if (!has && c >= 'A' && c <= 'Z')
                        has = re_class_has(re, in, c + 32);
                    take = has != in->z;
                }
                break;
            default:
                break;
            }
            if (take)
            {
                memcpy(re->saves, th + 1, (size_t)re->slots * sizeof(long));
                re_add_thread(re, next, pc + 1, text, length, at + size);
            }
        }
        list = next;
        if (c < 0)
            break;
        at += size;
    }
    return found;
}
