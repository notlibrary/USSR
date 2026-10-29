#ifndef _TIME_H
#define _TIME_H
#include <stddef.h>
typedef long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000000L
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
time_t time(time_t *t);
clock_t clock(void);
double difftime(time_t a, time_t b);
struct tm *localtime(const time_t *t);
struct tm *gmtime(const time_t *t);
#endif
