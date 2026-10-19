CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic
PETERSBURG_CFLAGS := $(shell pkg-config --cflags glfw3 gl 2>/dev/null)
PETERSBURG_LIBS := $(shell pkg-config --libs glfw3 gl 2>/dev/null)
ifneq ($(strip $(PETERSBURG_LIBS)),)
CFLAGS += -DUSSR_WITH_GLFW $(PETERSBURG_CFLAGS)
LDLIBS += $(PETERSBURG_LIBS)
endif
ifneq ($(PETERSBURG_BUNDLE),)
ifeq ($(strip $(PETERSBURG_LIBS)),)
$(error PETERSBURG_BUNDLE=1 requires GLFW and OpenGL development files discoverable through pkg-config)
endif
LDFLAGS += -Wl,-rpath,'$$ORIGIN/lib'
endif
TARGET  = ussr
PREFIX  = /usr
SRC_DIR = src
VERSION ?= $(shell awk -F'[][]' '/^AC_INIT/ { print $$4; exit }' configure.ac)
URAL_BUILD_DIR ?= build/ural
URAL_BIN = $(URAL_BUILD_DIR)/ural
URAL_SOURCES = $(wildcard src/ural/CMakeLists.txt src/ural/native/*.c src/ural/native/*.h)

OBJS = $(SRC_DIR)/parser.tab.o \
       $(SRC_DIR)/lex.yy.o \
       $(SRC_DIR)/ussr.o \
       $(SRC_DIR)/bestline.o \
       $(SRC_DIR)/main.o \
       $(SRC_DIR)/pp.o \
       $(SRC_DIR)/uno.o \
       $(SRC_DIR)/ussr_oop_builtins.o \
       $(SRC_DIR)/ussr_bytecode.o \
       $(SRC_DIR)/ussr_uuasm.o \
       $(SRC_DIR)/scheduler.o \
       $(SRC_DIR)/petersburg.o \
       $(SRC_DIR)/tver.o \
       $(SRC_DIR)/tula.o \
       $(SRC_DIR)/eventq.o \
       $(SRC_DIR)/gc.o \
       $(SRC_DIR)/prng64_xrp32.o \
       $(SRC_DIR)/autocomplete.o		  

ifeq ($(OS),Windows_NT)
OBJS += $(SRC_DIR)/completion_fs_win32.o $(SRC_DIR)/socket_win32.o $(SRC_DIR)/win/process_win32.o
LDLIBS += -lws2_32
else
OBJS += $(SRC_DIR)/completion_fs_posix.o $(SRC_DIR)/socket_posix.o $(SRC_DIR)/process_posix.o
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lm $(LDLIBS)

$(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.tab.h: $(SRC_DIR)/parser.y
	bison -d -Wall -o $(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.y

$(SRC_DIR)/lex.yy.c: $(SRC_DIR)/lexer.l $(SRC_DIR)/parser.tab.h
	flex -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/lexer.l

$(SRC_DIR)/parser.tab.o: $(SRC_DIR)/parser.tab.c $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/lex.yy.o: $(SRC_DIR)/lex.yy.c $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr.o: $(SRC_DIR)/ussr.c $(SRC_DIR)/ussr.h $(SRC_DIR)/uno.h $(SRC_DIR)/ussr_oop_builtins.h $(SRC_DIR)/uthash.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr_bytecode.o: $(SRC_DIR)/ussr_bytecode.c $(SRC_DIR)/ussr_bytecode.h $(SRC_DIR)/ussr_uuasm.h $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr_uuasm.o: $(SRC_DIR)/ussr_uuasm.c $(SRC_DIR)/ussr_uuasm.h $(SRC_DIR)/ussr_bytecode.h $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/autocomplete.o: $(SRC_DIR)/autocomplete.c $(SRC_DIR)/autocomplete.h $(SRC_DIR)/completion_fs.h $(SRC_DIR)/bestline.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/completion_fs_posix.o: $(SRC_DIR)/completion_fs_posix.c $(SRC_DIR)/completion_fs.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/completion_fs_win32.o: $(SRC_DIR)/completion_fs_win32.c $(SRC_DIR)/completion_fs.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/main.o: $(SRC_DIR)/main.c $(SRC_DIR)/ussr.h $(SRC_DIR)/ussr_bytecode.h $(SRC_DIR)/ussr_uuasm.h $(SRC_DIR)/vm.h $(SRC_DIR)/pp.h $(SRC_DIR)/uno.h $(SRC_DIR)/ussr_oop_builtins.h $(SRC_DIR)/autocomplete.h $(SRC_DIR)/bestline.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/pp.o: $(SRC_DIR)/pp.c $(SRC_DIR)/pp.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/uno.o: $(SRC_DIR)/uno.c $(SRC_DIR)/uno.h $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr_oop_builtins.o: $(SRC_DIR)/ussr_oop_builtins.c $(SRC_DIR)/ussr_oop_builtins.h $(SRC_DIR)/ussr.h $(SRC_DIR)/uno.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/bestline.o: $(SRC_DIR)/bestline.c $(SRC_DIR)/bestline.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/process_posix.o: $(SRC_DIR)/process_posix.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/win/process_win32.o: $(SRC_DIR)/win/process_win32.c $(SRC_DIR)/process.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/scheduler.o: $(SRC_DIR)/scheduler.c $(SRC_DIR)/scheduler.h $(SRC_DIR)/petersburg.h $(SRC_DIR)/tver.h $(SRC_DIR)/tula.h $(SRC_DIR)/vm.h $(SRC_DIR)/eventq.h $(SRC_DIR)/gc.h $(SRC_DIR)/socket.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/petersburg.o: $(SRC_DIR)/petersburg.c $(SRC_DIR)/petersburg.h $(SRC_DIR)/scheduler.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/tver.o: $(SRC_DIR)/tver.c $(SRC_DIR)/tver.h $(SRC_DIR)/petersburg.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/tula.o: $(SRC_DIR)/tula.c $(SRC_DIR)/tula.h $(SRC_DIR)/petersburg.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/eventq.o: $(SRC_DIR)/eventq.c $(SRC_DIR)/eventq.h $(SRC_DIR)/process.h $(SRC_DIR)/socket.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/socket_posix.o: $(SRC_DIR)/socket_posix.c $(SRC_DIR)/socket.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/socket_win32.o: $(SRC_DIR)/socket_win32.c $(SRC_DIR)/socket.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/gc.o: $(SRC_DIR)/gc.c $(SRC_DIR)/gc.h $(SRC_DIR)/uno.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/prng64_xrp32.o: $(SRC_DIR)/prng64_xrp32.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(SRC_DIR)/*.o $(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.tab.h $(SRC_DIR)/lex.yy.c $(TARGET).tar.gz ussr-linux-bin-$(VERSION).tar.gz

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

$(URAL_BIN): $(URAL_SOURCES)
	cmake -S src/ural -B $(URAL_BUILD_DIR) -DURAL_STATIC_DEPS=OFF -DBUILD_TESTING=OFF
	cmake --build $(URAL_BUILD_DIR) --parallel

dist: $(TARGET) $(URAL_BIN)
	@set -eu; \
	stage=$$(mktemp -d); \
	state=$$(mktemp -d); \
	trap 'rm -rf "$$stage" "$$state"' EXIT; \
	mkdir -p "$$stage/ussr-linux-bin"; \
	HOME="$$state" "$(CURDIR)/$(URAL_BIN)" init; \
	cp "$(TARGET)" "$(URAL_BIN)" src/ural/ural.su docs/ussr_user_manual.md moscow.su "$$stage/ussr-linux-bin/"; \
	cp "$$state/.ussr/ussr.boxes" "$$stage/ussr-linux-bin/ussr.boxes"; \
	if test -n "$(PETERSBURG_BUNDLE)"; then \
		glfw_libdir=$$(pkg-config --variable=libdir glfw3); \
		glfw_library=$$(readlink -f "$$glfw_libdir/libglfw.so.3"); \
		test -n "$$glfw_library" && test -f "$$glfw_library" || { echo "Could not locate the GLFW runtime library" >&2; exit 1; }; \
		mkdir -p "$$stage/ussr-linux-bin/lib"; \
		cp "$$glfw_library" "$$stage/ussr-linux-bin/lib/libglfw.so.3"; \
	fi; \
	if test -f README.md; then cp README.md "$$stage/ussr-linux-bin/"; fi; \
	if test -f CHANGELOG.md; then cp CHANGELOG.md "$$stage/ussr-linux-bin/"; fi; \
	if test -f LICENSE; then cp LICENSE "$$stage/ussr-linux-bin/"; fi; \
	tar -czf "ussr-linux-bin-$(VERSION).tar.gz" -C "$$stage" ussr-linux-bin

.PHONY: all clean install uninstall dist
