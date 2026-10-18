AC_INIT([ussr], [0.4.6], [yendorium@gmail.com])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CC
AM_PROG_LEX
AC_PROG_YACC
AC_PROG_INSTALL
PETERSBURG_CPPFLAGS=
PETERSBURG_LIBS=
AC_CHECK_HEADER([GLFW/glfw3.h], [petersburg_have_header=yes],
                [petersburg_have_header=no])
AC_CHECK_LIB([glfw], [glfwInit], [petersburg_have_glfw=yes],
             [petersburg_have_glfw=no])
AC_CHECK_LIB([GL], [glClear], [petersburg_have_gl=yes],
             [petersburg_have_gl=no])
AS_IF([test "x$petersburg_have_header" = xyes &&
       test "x$petersburg_have_glfw" = xyes &&
       test "x$petersburg_have_gl" = xyes], [
    PETERSBURG_CPPFLAGS="-DUSSR_WITH_GLFW"
    PETERSBURG_LIBS="-lglfw -lGL"
], [
    AC_MSG_NOTICE([GLFW/OpenGL development files not found; Petersburg backend disabled])
])
AC_SUBST([PETERSBURG_CPPFLAGS])
AC_SUBST([PETERSBURG_LIBS])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
