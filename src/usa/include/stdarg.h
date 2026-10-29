#ifndef _STDARG_H
#define _STDARG_H
/* the variadic arguments are 8-byte cells on the stack */
typedef __builtin_va_list va_list;
#define va_start(ap, last) ((ap) = __builtin_va_start())
#define va_arg(ap, type) (*(type *)(((ap) += 8) - 8))
#define va_end(ap) ((void)0)
#define va_copy(d, s) ((d) = (s))
#define __va_copy(d, s) ((d) = (s))
#endif
