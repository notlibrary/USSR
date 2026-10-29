#ifndef _LOCALE_H
#define _LOCALE_H
#define LC_ALL 0
#define LC_CTYPE 1
#define LC_NUMERIC 2
#define LC_TIME 3
#define LC_COLLATE 4
#define LC_MONETARY 5
struct lconv { char *decimal_point; char *thousands_sep; };
char *setlocale(int cat, const char *loc);
struct lconv *localeconv(void);
#endif
