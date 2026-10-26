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
RYAZAN_CPPFLAGS=
RYAZAN_LIBS=
AC_ARG_WITH([openssl],
    [AS_HELP_STRING([--without-openssl],
                    [build without Ryazan TLS (no https:// or tls_* builtins)])],
    [],
    [with_openssl=check])
AS_IF([test "x$with_openssl" != xno], [
    AC_CHECK_HEADER([openssl/ssl.h], [ryazan_have_header=yes],
                    [ryazan_have_header=no])
    AC_CHECK_LIB([ssl], [SSL_CTX_new], [ryazan_have_ssl=yes],
                 [ryazan_have_ssl=no], [-lcrypto])
    AS_IF([test "x$ryazan_have_header" = xyes && test "x$ryazan_have_ssl" = xyes], [
        RYAZAN_CPPFLAGS="-DUSSR_TLS_OPENSSL"
        RYAZAN_LIBS="-lssl -lcrypto"
    ], [test "x$with_openssl" = xyes], [
        AC_MSG_ERROR([Ryazan TLS requires OpenSSL development files; install libssl-dev, or configure with --without-openssl])
    ], [
        AC_MSG_WARN([OpenSSL not found: building without Ryazan TLS])
    ])
])
AC_SUBST([RYAZAN_CPPFLAGS])
AC_SUBST([RYAZAN_LIBS])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_OUTPUT
