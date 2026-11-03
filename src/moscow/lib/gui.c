/* gui.c - /lib/gui.so: Petersburg, USSR's GUI toolkit (src/petersburg.c,
 * the hosted ussr's own code), on Moscow. One translation unit: a small
 * C library, OpenGL in software, GLFW on the VGA framebuffer with the
 * PS/2 keyboard and mouse, Petersburg itself, Mbltishi's terminal, and
 * the functions src/moscow/gui.su calls with c_call. */

#define USSR_WITH_GLFW 1

#include "gui/shim.h"
#include "gui/libc.c"
#include "gui/gl.c"
#include "gui/glfw.c"
#include "../../petersburg.c"
#include "gui/term.c"
#include "gui/moscow.c"
