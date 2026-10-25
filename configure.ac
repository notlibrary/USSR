AC_INIT([ussr], [0.4.8], [yendorium@gmail.com])
AM_INIT_AUTOMAKE([foreign])
AC_PROG_CC
AM_PROG_LEX
AC_PROG_YACC
AC_PROG_INSTALL
PETERSBURG_CPPFLAGS=
PETERSBURG_LIBS=
AC_ARG_ENABLE([petersburg],
    [AS_HELP_STRING([--disable-petersburg],
                    [build without the Petersburg GLFW/OpenGL GUI backend])],
    [],
    [enable_petersburg=yes])
AS_IF([test "x$enable_petersburg" != xno], [
    AC_CHECK_HEADER([GLFW/glfw3.h], [petersburg_have_header=yes],
                    [petersburg_have_header=no])
    AC_CHECK_LIB([glfw], [glfwInit], [petersburg_have_glfw=yes],
                 [petersburg_have_glfw=no])
    AC_CHECK_LIB([GL], [glClear], [petersburg_have_gl=yes],
                 [petersburg_have_gl=no])
])
AS_IF([test "x$enable_petersburg" = xno], [
    AC_MSG_NOTICE([Petersburg GUI backend disabled by request])
], [
    AS_IF([test "x$petersburg_have_header" = xyes &&
           test "x$petersburg_have_glfw" = xyes &&
           test "x$petersburg_have_gl" = xyes], [
        PETERSBURG_CPPFLAGS="-DUSSR_WITH_GLFW"
        PETERSBURG_LIBS="-lglfw -lGL"
    ], [
        AC_MSG_ERROR([Petersburg GUI backend requires GLFW and OpenGL development files; install libglfw3-dev and libgl1-mesa-dev, or configure with --disable-petersburg])
    ])
])
AC_SUBST([PETERSBURG_CPPFLAGS])
AC_SUBST([PETERSBURG_LIBS])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
