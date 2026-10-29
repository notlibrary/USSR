#ifndef _STDDEF_H
#define _STDDEF_H
#define NULL ((void *)0)
#define offsetof(type, member) ((size_t)&((type *)0)->member)
typedef int wchar_t;
typedef long max_align_t;
#endif
