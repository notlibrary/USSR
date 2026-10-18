bin_PROGRAMS = ussr

ussr_SOURCES = \
    parser.y \
    lexer.l \
    ussr.c \
    ussr_bytecode.c \
    ussr_uuasm.c \
    bestline.c \
    main.c \
    pp.c \
    uno.c \
    ussr_oop_builtins.c \
    scheduler.c \
    petersburg.c \
    tver.c \
    eventq.c \
    socket_posix.c \
    gc.c \
	process_posix.c \
    prng64_xrp32.c \
	autocomplete.c
	
AM_CPPFLAGS = -I$(srcdir) $(PETERSBURG_CPPFLAGS)
ussr_LDADD = $(PETERSBURG_LIBS) -lm

EXTRA_DIST = \
    ussr.h \
    ussr_bytecode.h \
    ussr_uuasm.h \
    uno.h \
    ussr_oop_builtins.h \
    scheduler.h \
    petersburg.h \
    tver.h \
    eventq.h \
    socket.h \
    gc.h \
    vm.h \
    pp.h \
    bestline.h \
    uthash.h \
	prng64_xrp32.h \
	autocomplete.h \
    process.h \
	petersburg/petersburg.su \
	petersburg/box.uno \
	petersburg/README.md \
	petersburg/README-SUBPROJ.md \
	petersburg/CHANGELOG.md \
	petersburg/docs/API.md \
	petersburg/examples/hello.su \
	petersburg/tests/TESTPLAN.md \
	tver/tver.su \
	tver/box.uno \
	tver/README.md \
	tver/CHANGELOG.md \
	tver/docs/API.md \
	tver/examples/hello.su \
	tver/tests/TESTPLAN.md
