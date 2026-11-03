/* glfw.c - GLFW 3 for Moscow: a window system on the machine itself.
 *
 * The screen is the Bochs/QEMU standard VGA's linear framebuffer (PCI
 * 1234:1111, the DISPI registers at ports 0x1ce/0x1cf) at 32 bits a
 * pixel. The picture starts 256 KiB into video memory so that VGA text
 * mode's planes (the characters and the font) are left alone, and
 * gui_video_stop gives text mode back. The PS/2 controller is read here
 * while the GUI is on: keys become GLFW key codes and characters, the
 * mouse (a wheel mouse when it answers as one) moves a pointer.
 *
 * Each window has its own pixels, which gl.c draws into; glfwPollEvents
 * delivers input and puts the windows on the screen, bottom to top, the
 * decorated ones with a title bar: dragged by it, closed by its box. A
 * window goes to the top when clicked. */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <GLFW/glfw3.h>

/* the kernel's (arch/amd64/cffi.inc) */
extern long moscow_ticks(void);
extern void moscow_gui(long on);

static unsigned char petersburg_glyph(char character, int row);

#define GLFW_TITLE 24
#define GLFW_KEYS 512

struct GLFWwindow
{
    int x, y;               /* the content's top left on the screen */
    int width, height;
    uint32_t *pixels;
    char title[96];
    int visible;
    int decorated;
    int should_close;
    int maximized;
    int saved_x, saved_y, saved_w, saved_h;
    void *user;
    GLFWmousebuttonfun mouse_button;
    GLFWcharfun character;
    GLFWkeyfun key;
    GLFWwindowfocusfun focus;
    GLFWscrollfun scroll;
    struct GLFWwindow *above;
    struct GLFWwindow *below;
};

static struct
{
    int on;
    uint32_t *lfb;
    int width, height;
    int first_row;          /* rows of video memory before the picture */
    uint32_t *screen;       /* composed here, then copied */
    GLFWwindow *bottom;
    GLFWwindow *top;
    GLFWwindow *focused;
    GLFWwindow *current;
    int hint_visible;
    int hint_decorated;
    int cursor_x, cursor_y;
    int buttons;
    GLFWwindow *pressed;    /* where the button went down */
    GLFWwindow *dragging;
    int drag_dx, drag_dy;
    unsigned char keys[GLFW_KEYS];
    int e0;
    int shift, ctrl, alt;
    unsigned char packet[4];
    int packet_n;
    int packet_size;
    int dirty;
    GLFWvidmode mode;
} glfw;

/* titles in a TrueType font (moscow.c, once the kernel has handed one
 * over): 1 when it drew, else the bitmap font, which has capitals only */
static int (*glfw_title_draw)(int x, int y, const char *text, int max_width,
                              uint32_t color);

/* the terminal (moscow.c) may take a window's keys: 1 when it did */
static int (*glfw_key_filter)(GLFWwindow *window, int key, int action, int mods,
                              unsigned codepoint);

/* --- the hardware */

static uint32_t pci_read(int bus, int dev, int fn, int reg)
{
    port_out32(0xcf8, 0x80000000u | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                      ((uint32_t)fn << 8) | ((uint32_t)reg & 0xfc));
    return port_in32(0xcfc);
}

static void dispi(int index, int value)
{
    port_out16(0x1ce, (uint16_t)index);
    port_out16(0x1cf, (uint16_t)value);
}

static int dispi_get(int index)
{
    port_out16(0x1ce, (uint16_t)index);
    return port_in16(0x1cf);
}

/* the standard VGA's framebuffer, or 0 */
static uintptr_t vga_lfb(void)
{
    int bus, dev;
    for (bus = 0; bus < 4; ++bus)
        for (dev = 0; dev < 32; ++dev)
        {
            uint32_t id = pci_read(bus, dev, 0, 0);
            if (id == 0x11111234u)
                return pci_read(bus, dev, 0, 0x10) & ~(uint32_t)15;
        }
    return 0;
}

/* VGA text mode 3, 80x25, as the BIOS leaves it */
static const unsigned char vga_seq3[5] = { 0x03, 0x00, 0x03, 0x00, 0x02 };
static const unsigned char vga_crtc3[25] = {
    0x5f, 0x4f, 0x50, 0x82, 0x55, 0x81, 0xbf, 0x1f, 0x00, 0x4f, 0x0d, 0x0e,
    0x00, 0x00, 0x00, 0x50, 0x9c, 0x0e, 0x8f, 0x28, 0x1f, 0x96, 0xb9, 0xa3, 0xff
};
static const unsigned char vga_gc3[9] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0e, 0x00, 0xff };
static const unsigned char vga_ac3[21] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07, 0x38, 0x39, 0x3a, 0x3b,
    0x3c, 0x3d, 0x3e, 0x3f, 0x0c, 0x00, 0x0f, 0x08, 0x00
};

static void vga_text_mode(void)
{
    int i;
    port_out8(0x3c2, 0x67);
    for (i = 0; i < 5; ++i)
    {
        port_out8(0x3c4, (uint8_t)i);
        port_out8(0x3c5, vga_seq3[i]);
    }
    port_out8(0x3d4, 0x11);
    port_out8(0x3d5, port_in8(0x3d5) & 0x7f);
    for (i = 0; i < 25; ++i)
    {
        port_out8(0x3d4, (uint8_t)i);
        port_out8(0x3d5, vga_crtc3[i]);
    }
    for (i = 0; i < 9; ++i)
    {
        port_out8(0x3ce, (uint8_t)i);
        port_out8(0x3cf, vga_gc3[i]);
    }
    (void)port_in8(0x3da);
    for (i = 0; i < 21; ++i)
    {
        port_out8(0x3c0, (uint8_t)i);
        port_out8(0x3c0, vga_ac3[i]);
    }
    port_out8(0x3c0, 0x20);
}

/* --- the PS/2 controller */

static int ps2_wait_write(void)
{
    int spin;
    for (spin = 0; spin < 100000; ++spin)
        if ((port_in8(0x64) & 2) == 0)
            return 1;
    return 0;
}

static int ps2_read(void)
{
    int spin;
    for (spin = 0; spin < 200000; ++spin)
        if (port_in8(0x64) & 1)
            return port_in8(0x60);
    return -1;
}

/* a byte to the mouse; its answer */
static int ps2_mouse(int byte)
{
    if (!ps2_wait_write())
        return -1;
    port_out8(0x64, 0xd4);
    if (!ps2_wait_write())
        return -1;
    port_out8(0x60, (uint8_t)byte);
    return ps2_read();
}

static void ps2_mouse_start(void)
{
    int config;
    int id;

    ps2_wait_write();
    port_out8(0x64, 0xa8);              /* the second port on */
    ps2_wait_write();
    port_out8(0x64, 0x20);
    config = ps2_read();
    if (config >= 0)
    {
        config &= ~0x22;                /* its clock on, no interrupt: polled */
        ps2_wait_write();
        port_out8(0x64, 0x60);
        ps2_wait_write();
        port_out8(0x60, (uint8_t)config);
    }
    ps2_mouse(0xf6);
    /* 200, 100, 80: a wheel mouse answers to it as device 3 */
    ps2_mouse(0xf3); ps2_mouse(200);
    ps2_mouse(0xf3); ps2_mouse(100);
    ps2_mouse(0xf3); ps2_mouse(80);
    ps2_mouse(0xf2);
    id = ps2_read();
    glfw.packet_size = id == 3 ? 4 : 3;
    ps2_mouse(0xf3); ps2_mouse(100);
    ps2_mouse(0xf4);
    glfw.packet_n = 0;
}

/* --- starting and stopping */

/* the picture: width x height at 32 bits; 0, or -1 without a Bochs VGA */
long gui_video_start(long width, long height)
{
    uintptr_t lfb;
    int id;

    if (glfw.on)
        return 0;
    id = dispi_get(0);
    lfb = vga_lfb();
    if (id < 0xb0c0 || id > 0xb0cf || lfb == 0)
        return -1;
    glfw.screen = malloc((size_t)width * height * 4);
    if (glfw.screen == NULL)
        return -1;
    glfw.width = (int)width;
    glfw.height = (int)height;
    glfw.first_row = (int)((256 * 1024 + width * 4 - 1) / (width * 4));
    dispi(4, 0);
    dispi(1, (int)width);
    dispi(2, (int)height);
    dispi(3, 32);
    dispi(4, 0x01 | 0x40 | 0x80);       /* on, linear, memory kept */
    dispi(6, (int)width);
    dispi(7, (int)height + glfw.first_row);
    dispi(8, 0);
    dispi(9, glfw.first_row);
    glfw.lfb = (uint32_t *)(lfb + (uintptr_t)glfw.first_row * width * 4);
    glfw.mode.width = (int)width;
    glfw.mode.height = (int)height;
    glfw.mode.redBits = glfw.mode.greenBits = glfw.mode.blueBits = 8;
    glfw.mode.refreshRate = 60;
    glfw.cursor_x = (int)width / 2;
    glfw.cursor_y = (int)height / 2;
    glfw.on = 1;
    glfw.dirty = 1;
    moscow_gui(1);
    ps2_mouse_start();
    return 0;
}

/* text mode again */
long gui_video_stop(void)
{
    if (!glfw.on)
        return 0;
    ps2_mouse(0xf5);
    dispi(4, 0);
    vga_text_mode();
    free(glfw.screen);
    glfw.screen = NULL;
    glfw.on = 0;
    moscow_gui(0);
    return 0;
}

/* --- composing the screen */

static void glfw_fill(int x, int y, int w, int h, uint32_t color)
{
    int r, c;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > glfw.width) w = glfw.width - x;
    if (y + h > glfw.height) h = glfw.height - y;
    for (r = 0; r < h; ++r)
    {
        uint32_t *row = glfw.screen + (size_t)(y + r) * glfw.width + x;
        for (c = 0; c < w; ++c)
            row[c] = color;
    }
}

static void glfw_text(int x, int y, const char *text, int max_width, uint32_t color)
{
    int i;
    for (i = 0; text[i] != '\0' && (i + 1) * 7 <= max_width; ++i)
    {
        int row;
        for (row = 0; row < 7; ++row)
        {
            unsigned char bits = petersburg_glyph(text[i], row);
            int col;
            for (col = 0; col < 5; ++col)
                if (bits & (1u << (4 - col)))
                    glfw_fill(x + i * 7 + col, y + row, 1, 1, color);
        }
    }
}

static void glfw_blit(const GLFWwindow *w)
{
    int x0 = w->x, y0 = w->y, x1 = w->x + w->width, y1 = w->y + w->height;
    int r;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > glfw.width) x1 = glfw.width;
    if (y1 > glfw.height) y1 = glfw.height;
    if (x0 >= x1)
        return;
    for (r = y0; r < y1; ++r)
        memcpy(glfw.screen + (size_t)r * glfw.width + x0,
               w->pixels + (size_t)(r - w->y) * w->width + (x0 - w->x),
               (size_t)(x1 - x0) * 4);
}

static void glfw_frame(const GLFWwindow *w)
{
    int focused = w == glfw.focused;
    int tx = w->x - 1, ty = w->y - GLFW_TITLE, tw = w->width + 2;
    uint32_t bar = focused ? 0x2a4f86 : 0x3a3f4a;
    /* the border, the bar, the title and the close box */
    glfw_fill(tx - 1, ty - 1, tw + 2, w->height + GLFW_TITLE + 3, 0x101218);
    glfw_fill(tx, ty, tw, GLFW_TITLE, bar);
    if (glfw_title_draw == NULL ||
        !glfw_title_draw(tx + 10, ty + 4, w->title, tw - 50, 0xf0f2f6))
        glfw_text(tx + 10, ty + 9, w->title, tw - 50, 0xf0f2f6);
    glfw_fill(tx + tw - 22, ty + 4, 16, 16, focused ? 0xb03a2e : 0x5a5f6a);
    {
        int k;
        for (k = 3; k < 13; ++k)
        {
            glfw_fill(tx + tw - 22 + k, ty + 4 + k, 1, 1, 0xffffff);
            glfw_fill(tx + tw - 22 + 15 - k, ty + 4 + k, 1, 1, 0xffffff);
        }
    }
}

/* the pointer: an arrow, black edged */
static const char *glfw_arrow[19] = {
    "X", "XX", "X.X", "X..X", "X...X", "X....X", "X.....X", "X......X",
    "X.......X", "X........X", "X.........X", "X..........X", "X......XXXXX",
    "X...X..X", "X..XX..X", "X.X  X..X", "XX   X..X", "     X..X", "      XX"
};

static void glfw_pointer(void)
{
    int r, c;
    for (r = 0; r < 19; ++r)
        for (c = 0; glfw_arrow[r][c] != '\0'; ++c)
        {
            int x = glfw.cursor_x + c, y = glfw.cursor_y + r;
            char k = glfw_arrow[r][c];
            if (k == ' ' || x >= glfw.width || y >= glfw.height)
                continue;
            glfw.screen[(size_t)y * glfw.width + x] = k == 'X' ? 0x000000 : 0xffffff;
        }
}

/* everything onto the screen */
static void glfw_present(void)
{
    GLFWwindow *w;
    int covered = 0;
    int r;

    if (!glfw.on || !glfw.dirty)
        return;
    for (w = glfw.bottom; w != NULL; w = w->above)
        if (w->visible && !w->decorated && w->x <= 0 && w->y <= 0 &&
            w->width >= glfw.width && w->height >= glfw.height)
            covered = 1;
    if (!covered)
        glfw_fill(0, 0, glfw.width, glfw.height, 0x1d3b4a);
    for (w = glfw.bottom; w != NULL; w = w->above)
    {
        if (!w->visible)
            continue;
        if (w->decorated)
            glfw_frame(w);
        glfw_blit(w);
    }
    glfw_pointer();
    for (r = 0; r < glfw.height; ++r)
        memcpy(glfw.lfb + (size_t)r * glfw.width, glfw.screen + (size_t)r * glfw.width,
               (size_t)glfw.width * 4);
    glfw.dirty = 0;
}

/* --- windows */

static void glfw_unlink(GLFWwindow *w)
{
    if (w->below != NULL)
        w->below->above = w->above;
    else
        glfw.bottom = w->above;
    if (w->above != NULL)
        w->above->below = w->below;
    else
        glfw.top = w->below;
    w->above = w->below = NULL;
}

static void glfw_push_top(GLFWwindow *w)
{
    w->below = glfw.top;
    w->above = NULL;
    if (glfw.top != NULL)
        glfw.top->above = w;
    else
        glfw.bottom = w;
    glfw.top = w;
}

static void glfw_focus(GLFWwindow *w)
{
    GLFWwindow *old = glfw.focused;
    if (old == w)
        return;
    glfw.focused = w;
    glfw.dirty = 1;
    if (old != NULL && old->focus != NULL)
        old->focus(old, 0);
    if (w != NULL && w->focus != NULL)
        w->focus(w, 1);
}

static void glfw_raise(GLFWwindow *w)
{
    if (glfw.top != w)
    {
        glfw_unlink(w);
        glfw_push_top(w);
    }
    glfw_focus(w);
    glfw.dirty = 1;
}

/* the top visible window still there, for the focus */
static GLFWwindow *glfw_topmost(void)
{
    GLFWwindow *w;
    for (w = glfw.top; w != NULL; w = w->below)
        if (w->visible)
            return w;
    return NULL;
}

static int glfw_resize(GLFWwindow *w, int width, int height)
{
    uint32_t *pixels;
    if (width < 1) width = 1;
    if (height < 1) height = 1;
    if (width > glfw.width) width = glfw.width;
    if (height > glfw.height) height = glfw.height;
    if (w->pixels != NULL && width == w->width && height == w->height)
        return 1;
    pixels = calloc((size_t)width * height, 4);
    if (pixels == NULL)
        return 0;
    free(w->pixels);
    w->pixels = pixels;
    w->width = width;
    w->height = height;
    if (glfw.current == w)
        gl_bind(w, w->pixels, w->width, w->height);
    glfw.dirty = 1;
    return 1;
}

int glfwInit(void)
{
    glfw.hint_visible = 1;
    glfw.hint_decorated = 1;
    return glfw.on;
}

void glfwTerminate(void) {}

void glfwWindowHint(int hint, int value)
{
    if (hint == GLFW_VISIBLE)
        glfw.hint_visible = value;
    else if (hint == GLFW_DECORATED)
        glfw.hint_decorated = value;
}

GLFWwindow *glfwCreateWindow(int width, int height, const char *title,
                             GLFWmonitor *monitor, GLFWwindow *share)
{
    GLFWwindow *w;
    size_t n;
    (void)monitor;
    (void)share;
    if (!glfw.on)
        return NULL;
    w = calloc(1, sizeof(*w));
    if (w == NULL)
        return NULL;
    if (!glfw_resize(w, width, height))
    {
        free(w);
        return NULL;
    }
    n = strlen(title);
    if (n >= sizeof(w->title))
        n = sizeof(w->title) - 1;
    memcpy(w->title, title, n);
    w->decorated = glfw.hint_decorated;
    w->visible = glfw.hint_visible;
    /* in the middle of the screen, a step down and right of a window
     * already there */
    w->x = (glfw.width - w->width) / 2;
    w->y = (glfw.height - w->height + GLFW_TITLE) / 2;
    {
        GLFWwindow *o;
        int moved = 1;
        while (moved)
        {
            moved = 0;
            for (o = glfw.bottom; o != NULL; o = o->above)
                if (o->visible && o->x == w->x && o->y == w->y)
                {
                    w->x += 32;
                    w->y += 28;
                    moved = 1;
                }
        }
    }
    if (w->x + w->width > glfw.width)
        w->x = glfw.width - w->width;
    if (w->y + w->height > glfw.height)
        w->y = glfw.height - w->height;
    if (w->x < 0) w->x = 0;
    if (w->y < GLFW_TITLE) w->y = w->decorated ? GLFW_TITLE : 0;
    glfw_push_top(w);
    if (w->visible)
        glfw_focus(w);
    glfw.hint_visible = 1;
    glfw.hint_decorated = 1;
    glfw.dirty = 1;
    return w;
}

void glfwDestroyWindow(GLFWwindow *w)
{
    if (w == NULL)
        return;
    glfw_unlink(w);
    gl_forget(w);
    if (glfw.current == w)
        glfw.current = NULL;
    if (glfw.pressed == w)
        glfw.pressed = NULL;
    if (glfw.dragging == w)
        glfw.dragging = NULL;
    if (glfw.focused == w)
    {
        glfw.focused = NULL;
        glfw_focus(glfw_topmost());
    }
    free(w->pixels);
    free(w);
    glfw.dirty = 1;
}

int glfwWindowShouldClose(GLFWwindow *w) { return w->should_close; }
void glfwSetWindowShouldClose(GLFWwindow *w, int value) { w->should_close = value; }

void glfwShowWindow(GLFWwindow *w)
{
    if (!w->visible)
    {
        w->visible = 1;
        glfw_raise(w);
    }
}

void glfwMakeContextCurrent(GLFWwindow *w)
{
    glfw.current = w;
    if (w != NULL)
        gl_bind(w, w->pixels, w->width, w->height);
}

GLFWwindow *glfwGetCurrentContext(void) { return glfw.current; }
void glfwSwapBuffers(GLFWwindow *w) { if (w->visible) glfw.dirty = 1; }
void glfwSwapInterval(int interval) { (void)interval; }

void glfwGetFramebufferSize(GLFWwindow *w, int *width, int *height)
{
    *width = w->width;
    *height = w->height;
}

void glfwGetWindowSize(GLFWwindow *w, int *width, int *height)
{
    *width = w->width;
    *height = w->height;
}

void glfwSetWindowSize(GLFWwindow *w, int width, int height)
{
    glfw_resize(w, width, height);
}

void glfwSetWindowPos(GLFWwindow *w, int x, int y)
{
    w->x = x;
    w->y = y;
    glfw.dirty = 1;
}

void glfwSetWindowAttrib(GLFWwindow *w, int attrib, int value)
{
    if (attrib == GLFW_DECORATED)
        w->decorated = value;
    glfw.dirty = 1;
}

void glfwMaximizeWindow(GLFWwindow *w)
{
    if (!w->maximized)
    {
        w->saved_x = w->x;
        w->saved_y = w->y;
        w->saved_w = w->width;
        w->saved_h = w->height;
    }
    w->maximized = 1;
    w->x = 0;
    w->y = w->decorated ? GLFW_TITLE : 0;
    glfw_resize(w, glfw.width, glfw.height - w->y);
}

void glfwRestoreWindow(GLFWwindow *w)
{
    if (!w->maximized)
        return;
    w->maximized = 0;
    w->x = w->saved_x;
    w->y = w->saved_y;
    glfw_resize(w, w->saved_w, w->saved_h);
}

void glfwGetCursorPos(GLFWwindow *w, double *x, double *y)
{
    *x = glfw.cursor_x - w->x;
    *y = glfw.cursor_y - w->y;
}

int glfwGetKey(GLFWwindow *w, int key)
{
    return w == glfw.focused && key >= 0 && key < GLFW_KEYS && glfw.keys[key];
}

int glfwGetMouseButton(GLFWwindow *w, int button)
{
    (void)w;
    return (glfw.buttons >> button) & 1;
}

void glfwSetWindowUserPointer(GLFWwindow *w, void *p) { w->user = p; }
void *glfwGetWindowUserPointer(GLFWwindow *w) { return w->user; }

GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow *w, GLFWmousebuttonfun f)
{
    GLFWmousebuttonfun old = w->mouse_button;
    w->mouse_button = f;
    return old;
}

GLFWcharfun glfwSetCharCallback(GLFWwindow *w, GLFWcharfun f)
{
    GLFWcharfun old = w->character;
    w->character = f;
    return old;
}

GLFWkeyfun glfwSetKeyCallback(GLFWwindow *w, GLFWkeyfun f)
{
    GLFWkeyfun old = w->key;
    w->key = f;
    return old;
}

GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow *w, GLFWwindowfocusfun f)
{
    GLFWwindowfocusfun old = w->focus;
    w->focus = f;
    return old;
}

GLFWscrollfun glfwSetScrollCallback(GLFWwindow *w, GLFWscrollfun f)
{
    GLFWscrollfun old = w->scroll;
    w->scroll = f;
    return old;
}

static int glfw_monitor_token;
GLFWmonitor *glfwGetPrimaryMonitor(void)
{
    return glfw.on ? (GLFWmonitor *)&glfw_monitor_token : NULL;
}

const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *monitor)
{
    (void)monitor;
    return &glfw.mode;
}

void glfwGetMonitorPos(GLFWmonitor *monitor, int *x, int *y)
{
    (void)monitor;
    *x = 0;
    *y = 0;
}

double glfwGetTime(void) { return (double)moscow_ticks() / 100.0; }

/* --- input */

/* set 1 scan codes: GLFW keys, then the extended (0xe0) ones */
static const short glfw_scan[0x59] = {
    0, GLFW_KEY_ESCAPE, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',
    GLFW_KEY_BACKSPACE, GLFW_KEY_TAB,
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '[', ']', GLFW_KEY_ENTER,
    GLFW_KEY_LEFT_CONTROL,
    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '`', GLFW_KEY_LEFT_SHIFT, '\\',
    'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '/', GLFW_KEY_RIGHT_SHIFT,
    332 /* KP * */, GLFW_KEY_LEFT_ALT, ' ', GLFW_KEY_CAPS_LOCK,
    290, 291, 292, 293, 294, 295, 296, 297, 298, 299, 282 /* num lock */, 281 /* scroll lock */,
    327, 328, 329, 333, 324, 325, 326, 334, 321, 322, 323, 320, 330, /* keypad */
    0, 0, 0, 300, 301
};

static int glfw_extended(int code)
{
    switch (code)
    {
    case 0x1c: return GLFW_KEY_KP_ENTER;
    case 0x1d: return GLFW_KEY_RIGHT_CONTROL;
    case 0x38: return GLFW_KEY_RIGHT_ALT;
    case 0x35: return 331;      /* KP / */
    case 0x47: return GLFW_KEY_HOME;
    case 0x48: return GLFW_KEY_UP;
    case 0x49: return GLFW_KEY_PAGE_UP;
    case 0x4b: return GLFW_KEY_LEFT;
    case 0x4d: return GLFW_KEY_RIGHT;
    case 0x4f: return GLFW_KEY_END;
    case 0x50: return GLFW_KEY_DOWN;
    case 0x51: return GLFW_KEY_PAGE_DOWN;
    case 0x52: return GLFW_KEY_INSERT;
    case 0x53: return GLFW_KEY_DELETE;
    default: return 0;
    }
}

/* the character a key types, 0 for none */
static unsigned glfw_char(int key)
{
    static const char plain[] = "`1234567890-=[]\\;',./";
    static const char shifted[] = "~!@#$%^&*()_+{}|:\"<>?";
    const char *p;
    if (glfw.ctrl || glfw.alt)
        return 0;
    if (key >= 'A' && key <= 'Z')
        return glfw.shift ? (unsigned)key : (unsigned)(key + 32);
    if (key == ' ')
        return ' ';
    if (key >= 320 && key <= 329)
        return (unsigned)('0' + key - 320);
    if (key == 330) return '.';
    if (key == 331) return '/';
    if (key == 332) return '*';
    if (key == 333) return '-';
    if (key == 334) return '+';
    if (key < 128 && (p = strchr(plain, key)) != NULL)
        return glfw.shift ? (unsigned char)shifted[p - plain] : (unsigned)key;
    return 0;
}

static int glfw_mods(void)
{
    return (glfw.shift ? GLFW_MOD_SHIFT : 0) | (glfw.ctrl ? GLFW_MOD_CONTROL : 0) |
           (glfw.alt ? GLFW_MOD_ALT : 0);
}

static void glfw_key_event(int key, int down)
{
    GLFWwindow *w = glfw.focused;
    int action;
    unsigned ch;

    if (key <= 0 || key >= GLFW_KEYS)
        return;
    action = !down ? GLFW_RELEASE : glfw.keys[key] ? GLFW_REPEAT : GLFW_PRESS;
    glfw.keys[key] = (unsigned char)down;
    if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT)
        glfw.shift = glfw.keys[GLFW_KEY_LEFT_SHIFT] || glfw.keys[GLFW_KEY_RIGHT_SHIFT];
    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL)
        glfw.ctrl = glfw.keys[GLFW_KEY_LEFT_CONTROL] || glfw.keys[GLFW_KEY_RIGHT_CONTROL];
    if (key == GLFW_KEY_LEFT_ALT || key == GLFW_KEY_RIGHT_ALT)
        glfw.alt = glfw.keys[GLFW_KEY_LEFT_ALT] || glfw.keys[GLFW_KEY_RIGHT_ALT];
    if (w == NULL)
        return;
    ch = down ? glfw_char(key) : 0;
    if (glfw_key_filter != NULL && glfw_key_filter(w, key, action, glfw_mods(), ch))
        return;
    if (w->key != NULL)
        w->key(w, key, 0, action, glfw_mods());
    if (ch != 0 && w->character != NULL && glfw.focused == w)
        w->character(w, ch);
}

static void glfw_scancode(int code)
{
    int key;
    if (code == 0xe0)
    {
        glfw.e0 = 1;
        return;
    }
    if (glfw.e0)
    {
        glfw.e0 = 0;
        key = glfw_extended(code & 0x7f);
    }
    else
        key = (code & 0x7f) < 0x59 ? glfw_scan[code & 0x7f] : 0;
    glfw_key_event(key, (code & 0x80) == 0);
}

/* the visible window at a point of the screen, and whether on its bar */
static GLFWwindow *glfw_window_at(int x, int y, int *on_bar)
{
    GLFWwindow *w;
    for (w = glfw.top; w != NULL; w = w->below)
    {
        int top = w->decorated ? w->y - GLFW_TITLE : w->y;
        if (!w->visible || x < w->x - 1 || x > w->x + w->width ||
            y < top || y >= w->y + w->height)
            continue;
        *on_bar = y < w->y;
        return w;
    }
    *on_bar = 0;
    return NULL;
}

static void glfw_button(int button, int down)
{
    int bit = 1 << button;
    int on_bar;
    GLFWwindow *w;

    if (((glfw.buttons & bit) != 0) == down)
        return;
    glfw.buttons = down ? glfw.buttons | bit : glfw.buttons & ~bit;
    if (!down)
    {
        if (button == 0)
            glfw.dragging = NULL;
        w = glfw.pressed;
        if (w != NULL && w->mouse_button != NULL)
            w->mouse_button(w, button, GLFW_RELEASE, glfw_mods());
        if (glfw.buttons == 0)
            glfw.pressed = NULL;
        return;
    }
    w = glfw_window_at(glfw.cursor_x, glfw.cursor_y, &on_bar);
    if (w == NULL)
        return;
    glfw_raise(w);
    if (on_bar)
    {
        if (button == 0 && glfw.cursor_x >= w->x + w->width - 22)
            w->should_close = 1;
        else if (button == 0)
        {
            glfw.dragging = w;
            glfw.drag_dx = glfw.cursor_x - w->x;
            glfw.drag_dy = glfw.cursor_y - w->y;
        }
        return;
    }
    glfw.pressed = w;
    if (w->mouse_button != NULL)
        w->mouse_button(w, button, GLFW_PRESS, glfw_mods());
}

static void glfw_mouse_packet(void)
{
    int b0 = glfw.packet[0];
    int dx = glfw.packet[1] - ((b0 << 4) & 0x100);
    int dy = glfw.packet[2] - ((b0 << 3) & 0x100);

    if ((b0 & 0xc0) == 0)
    {
        glfw.cursor_x += dx;
        glfw.cursor_y -= dy;
        if (glfw.cursor_x < 0) glfw.cursor_x = 0;
        if (glfw.cursor_y < 0) glfw.cursor_y = 0;
        if (glfw.cursor_x >= glfw.width) glfw.cursor_x = glfw.width - 1;
        if (glfw.cursor_y >= glfw.height) glfw.cursor_y = glfw.height - 1;
        if (dx != 0 || dy != 0)
        {
            glfw.dirty = 1;
            if (glfw.dragging != NULL)
            {
                glfw.dragging->x = glfw.cursor_x - glfw.drag_dx;
                glfw.dragging->y = glfw.cursor_y - glfw.drag_dy;
                if (glfw.dragging->y < GLFW_TITLE)
                    glfw.dragging->y = GLFW_TITLE;
            }
        }
    }
    glfw_button(0, b0 & 1);
    glfw_button(1, (b0 >> 1) & 1);
    glfw_button(2, (b0 >> 2) & 1);
    if (glfw.packet_size == 4)
    {
        int z = glfw.packet[3] & 15;
        int on_bar;
        GLFWwindow *w;
        if (z >= 8)
            z -= 16;
        w = glfw_window_at(glfw.cursor_x, glfw.cursor_y, &on_bar);
        if (z != 0 && w != NULL && !on_bar && w->scroll != NULL)
            w->scroll(w, 0.0, z < 0 ? 1.0 : -1.0);
    }
}

/* everything the controller has */
static void glfw_input(void)
{
    int spin;
    for (spin = 0; spin < 512; ++spin)
    {
        uint8_t status = port_in8(0x64);
        uint8_t byte;
        if ((status & 1) == 0)
            break;
        byte = port_in8(0x60);
        if (status & 0x20)
        {
            if (glfw.packet_n == 0 && (byte & 8) == 0)
                continue;       /* not the first byte of a packet */
            glfw.packet[glfw.packet_n++] = byte;
            if (glfw.packet_n == glfw.packet_size)
            {
                glfw.packet_n = 0;
                glfw_mouse_packet();
            }
        }
        else
            glfw_scancode(byte);
    }
}

void glfwPollEvents(void)
{
    if (!glfw.on)
        return;
    glfw_input();
    glfw_present();
}

void glfwWaitEventsTimeout(double timeout)
{
    (void)timeout;
    glfwPollEvents();
}
