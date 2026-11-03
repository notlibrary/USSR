/* GLFW/glfw3.h for Moscow: the part of GLFW 3 and of OpenGL 1.1 that
 * Petersburg (src/petersburg.c) uses, with GLFW's own names and values.
 * lib/gui/glfw.c is the window system (windows composed on the VGA
 * framebuffer, the PS/2 keyboard and mouse); lib/gui/gl.c draws them in
 * software. */
#ifndef MOSCOW_GLFW3_H
#define MOSCOW_GLFW3_H

/* --- OpenGL 1.1 */

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef unsigned int GLbitfield;
typedef void GLvoid;

#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE_2D 0x0DE1
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_CLAMP 0x2900
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908

void glBegin(GLenum mode);
void glEnd(void);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex2d(GLdouble x, GLdouble y);
void glColor3f(GLfloat r, GLfloat g, GLfloat b);
void glTexCoord2f(GLfloat s, GLfloat t);
void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glViewport(GLint x, GLint y, GLsizei w, GLsizei h);
void glScissor(GLint x, GLint y, GLsizei w, GLsizei h);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glBlendFunc(GLenum s, GLenum d);
void glClearColor(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glClear(GLbitfield mask);
void glGenTextures(GLsizei n, GLuint *ids);
void glDeleteTextures(GLsizei n, const GLuint *ids);
void glBindTexture(GLenum target, GLuint id);
void glTexParameteri(GLenum target, GLenum name, GLint value);
void glTexEnvi(GLenum target, GLenum name, GLint value);
void glTexImage2D(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h,
                  GLint border, GLenum format, GLenum type, const GLvoid *pixels);
void glPixelStorei(GLenum name, GLint value);
void glRasterPos2i(GLint x, GLint y);
void glPixelZoom(GLfloat x, GLfloat y);
void glDrawPixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid *pixels);

/* --- GLFW 3 */

typedef struct GLFWwindow GLFWwindow;
typedef struct GLFWmonitor GLFWmonitor;
typedef struct GLFWvidmode
{
    int width;
    int height;
    int redBits;
    int greenBits;
    int blueBits;
    int refreshRate;
} GLFWvidmode;

typedef void (*GLFWmousebuttonfun)(GLFWwindow *, int, int, int);
typedef void (*GLFWcharfun)(GLFWwindow *, unsigned int);
typedef void (*GLFWkeyfun)(GLFWwindow *, int, int, int, int);
typedef void (*GLFWwindowfocusfun)(GLFWwindow *, int);
typedef void (*GLFWscrollfun)(GLFWwindow *, double, double);

#define GLFW_TRUE 1
#define GLFW_FALSE 0
#define GLFW_RELEASE 0
#define GLFW_PRESS 1
#define GLFW_REPEAT 2

#define GLFW_MOUSE_BUTTON_LEFT 0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GLFW_MOUSE_BUTTON_MIDDLE 2

#define GLFW_MOD_SHIFT 0x0001
#define GLFW_MOD_CONTROL 0x0002
#define GLFW_MOD_ALT 0x0004

#define GLFW_FOCUSED 0x00020001
#define GLFW_VISIBLE 0x00020004
#define GLFW_DECORATED 0x00020005
#define GLFW_CONTEXT_VERSION_MAJOR 0x00022002
#define GLFW_CONTEXT_VERSION_MINOR 0x00022003

#define GLFW_KEY_UNKNOWN -1
#define GLFW_KEY_SPACE 32
#define GLFW_KEY_APOSTROPHE 39
#define GLFW_KEY_COMMA 44
#define GLFW_KEY_MINUS 45
#define GLFW_KEY_PERIOD 46
#define GLFW_KEY_SLASH 47
#define GLFW_KEY_0 48
#define GLFW_KEY_9 57
#define GLFW_KEY_SEMICOLON 59
#define GLFW_KEY_EQUAL 61
#define GLFW_KEY_A 65
#define GLFW_KEY_Z 90
#define GLFW_KEY_LEFT_BRACKET 91
#define GLFW_KEY_BACKSLASH 92
#define GLFW_KEY_RIGHT_BRACKET 93
#define GLFW_KEY_GRAVE_ACCENT 96
#define GLFW_KEY_ESCAPE 256
#define GLFW_KEY_ENTER 257
#define GLFW_KEY_TAB 258
#define GLFW_KEY_BACKSPACE 259
#define GLFW_KEY_INSERT 260
#define GLFW_KEY_DELETE 261
#define GLFW_KEY_RIGHT 262
#define GLFW_KEY_LEFT 263
#define GLFW_KEY_DOWN 264
#define GLFW_KEY_UP 265
#define GLFW_KEY_PAGE_UP 266
#define GLFW_KEY_PAGE_DOWN 267
#define GLFW_KEY_HOME 268
#define GLFW_KEY_END 269
#define GLFW_KEY_CAPS_LOCK 280
#define GLFW_KEY_F1 290
#define GLFW_KEY_KP_ENTER 335
#define GLFW_KEY_LEFT_SHIFT 340
#define GLFW_KEY_LEFT_CONTROL 341
#define GLFW_KEY_LEFT_ALT 342
#define GLFW_KEY_RIGHT_SHIFT 344
#define GLFW_KEY_RIGHT_CONTROL 345
#define GLFW_KEY_RIGHT_ALT 346

int glfwInit(void);
void glfwTerminate(void);
void glfwWindowHint(int hint, int value);
GLFWwindow *glfwCreateWindow(int width, int height, const char *title,
                             GLFWmonitor *monitor, GLFWwindow *share);
void glfwDestroyWindow(GLFWwindow *window);
int glfwWindowShouldClose(GLFWwindow *window);
void glfwSetWindowShouldClose(GLFWwindow *window, int value);
void glfwShowWindow(GLFWwindow *window);
void glfwMakeContextCurrent(GLFWwindow *window);
GLFWwindow *glfwGetCurrentContext(void);
void glfwSwapBuffers(GLFWwindow *window);
void glfwSwapInterval(int interval);
void glfwPollEvents(void);
void glfwWaitEventsTimeout(double timeout);
void glfwGetFramebufferSize(GLFWwindow *window, int *width, int *height);
void glfwGetWindowSize(GLFWwindow *window, int *width, int *height);
void glfwSetWindowSize(GLFWwindow *window, int width, int height);
void glfwSetWindowPos(GLFWwindow *window, int x, int y);
void glfwSetWindowAttrib(GLFWwindow *window, int attrib, int value);
void glfwMaximizeWindow(GLFWwindow *window);
void glfwRestoreWindow(GLFWwindow *window);
void glfwGetCursorPos(GLFWwindow *window, double *x, double *y);
int glfwGetKey(GLFWwindow *window, int key);
int glfwGetMouseButton(GLFWwindow *window, int button);
void glfwSetWindowUserPointer(GLFWwindow *window, void *pointer);
void *glfwGetWindowUserPointer(GLFWwindow *window);
GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow *window, GLFWmousebuttonfun f);
GLFWcharfun glfwSetCharCallback(GLFWwindow *window, GLFWcharfun f);
GLFWkeyfun glfwSetKeyCallback(GLFWwindow *window, GLFWkeyfun f);
GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow *window, GLFWwindowfocusfun f);
GLFWscrollfun glfwSetScrollCallback(GLFWwindow *window, GLFWscrollfun f);
GLFWmonitor *glfwGetPrimaryMonitor(void);
const GLFWvidmode *glfwGetVideoMode(GLFWmonitor *monitor);
void glfwGetMonitorPos(GLFWmonitor *monitor, int *x, int *y);
double glfwGetTime(void);

#endif
