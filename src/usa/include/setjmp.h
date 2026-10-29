#ifndef _SETJMP_H
#define _SETJMP_H
/* longjmp cannot unwind the VM's call frames: it ends the program */
typedef long jmp_buf[8];
#define setjmp(b) 0
void longjmp(jmp_buf b, int v);
#endif
