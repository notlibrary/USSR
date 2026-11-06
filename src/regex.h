/*
 * regex.h - regular expressions for USSR: re_test, re_find, re_pos, re_all,
 * re_replace and re_split. A pattern compiles to a small program that a Pike
 * VM runs, so a search takes time proportional to pattern size times text
 * size, whatever the pattern: no pattern can make it hang. That is why there
 * are no backreferences and no lookaround. Moscow's runtime has the same
 * engine in NASM (src/moscow/arch/amd64/regex.inc); tests/regex_test.su holds
 * both to one behaviour.
 */
#ifndef USSR_REGEX_H
#define USSR_REGEX_H

#include <stddef.h>

typedef struct ussr_regex_t ussr_regex_t;

/* flags: any of i (ASCII case), m (^ and $ at lines), s (. takes newlines),
 * or NULL. NULL back, with the reason in error, when it does not compile. */
ussr_regex_t *ussr_regex_compile(const char *pattern, const char *flags,
                                 char *error, size_t error_size);
void ussr_regex_free(ussr_regex_t *re);

/* capture groups, the whole match not counted */
int ussr_regex_groups(const ussr_regex_t *re);

/* The first match at or after from. spans gets a start and an end per group,
 * the whole match first, -1 for a group that took no part: 2 * (groups + 1)
 * longs. 1 when found, 0 when not, -1 out of memory. */
int ussr_regex_search(ussr_regex_t *re, const char *text, size_t length,
                      size_t from, long *spans);

/* the bytes of the character at text[at] */
size_t ussr_regex_char_length(const char *text, size_t length, size_t at);

#endif
