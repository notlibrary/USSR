bin_PROGRAMS = ussr

ussr_SOURCES = \
    parser.y \
    lexer.l \
    ussr.c \
    ussr_bytecode.c \
    ussr_uuasm.c \
    ussr_ru.c \
    bestline.c \
    main.c \
    pp.c \
    uno.c \
    ussr_oop_builtins.c \
    scheduler.c \
    petersburg.c \
    eventq.c \
    socket_posix.c \
    gc.c \
	process_posix.c \
    prng64_xrp32.c \
	autocomplete.c \
	keybind.c \
	alias.c \
	history.c \
	ai.c \
	help.c \
	completion_fs_posix.c
	
AM_CPPFLAGS = -I$(srcdir) $(PETERSBURG_CPPFLAGS)
ussr_LDADD = $(PETERSBURG_LIBS) -lm

# lexer.l includes parser.tab.h (bison's name, as ussr-ubuntu.mk and
# Makefile.msc generate it); automake's yacc rules write parser.h, so the
# header is copied under that name before anything is compiled.
AM_YFLAGS = -d
BUILT_SOURCES = parser.h parser.tab.h
parser.tab.h: parser.h
	cp $< $@
CLEANFILES = parser.tab.h

EXTRA_DIST = \
    ussr.h \
    ussr_bytecode.h \
    ussr_uuasm.h \
    ussr_ru.h \
    uno.h \
    ussr_oop_builtins.h \
    scheduler.h \
    petersburg.h \
    eventq.h \
    socket.h \
    gc.h \
    vm.h \
    pp.h \
    bestline.h \
    uthash.h \
	prng64_xrp32.h \
	autocomplete.h \
	completion_fs.h \
	ussr_version.h \
	keybind.h \
	alias.h \
	history.h \
	ai.h \
	help.h \
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
	tver/tests/TESTPLAN.md \
	tula/tula.su \
	tula/box.uno \
	tula/README.md \
	tula/CHANGELOG.md \
	tula/docs/API.md \
	tula/examples/hello.su \
	tula/tests/TESTPLAN.md \
	kgb/box.uno \
	klin/box.uno \
	kremlin/box.uno \
	novgorod/box.uno \
	ryazan/box.uno \
	siberia/box.uno \
	stalin/box.uno \
	usa/box.uno \
	sochi/box.uno
