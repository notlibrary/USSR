CC      = gcc
CFLAGS  = -std=c11 -O2 -flto=auto -Wall -Wextra -Wpedantic
# The Petersburg GUI is built in whenever pkg-config finds GLFW and OpenGL
# (libglfw3-dev, libgl1-mesa-dev); PETERSBURG_BUNDLE=1 requires them.
# The GUI build compiles petersburg.c to its own object, so installing or
# removing GLFW between builds never links an object built the other way.
PETERSBURG_CFLAGS := $(shell pkg-config --cflags glfw3 gl 2>/dev/null)
PETERSBURG_LIBS := $(shell pkg-config --libs glfw3 gl 2>/dev/null)
PETERSBURG_OBJ = $(SRC_DIR)/petersburg.o
ifneq ($(strip $(PETERSBURG_LIBS)),)
CFLAGS += -DUSSR_WITH_GLFW $(PETERSBURG_CFLAGS)
LDLIBS += $(PETERSBURG_LIBS)
PETERSBURG_OBJ = $(SRC_DIR)/petersburg_glfw.o
endif
ifneq ($(PETERSBURG_BUNDLE),)
ifeq ($(strip $(PETERSBURG_LIBS)),)
$(error PETERSBURG_BUNDLE=1 requires GLFW and OpenGL development files discoverable through pkg-config)
endif
LDFLAGS += -Wl,-rpath,'$$ORIGIN/lib'
endif
# Ryazan TLS: Schannel on Windows; elsewhere OpenSSL whenever pkg-config
# finds it (libssl-dev), otherwise https:// and tls_* report "no TLS".
# RYAZAN_REQUIRE_TLS=1 makes a missing OpenSSL an error.
ifneq ($(OS),Windows_NT)
RYAZAN_CFLAGS := $(shell pkg-config --cflags openssl 2>/dev/null)
RYAZAN_LIBS := $(shell pkg-config --libs openssl 2>/dev/null)
ifneq ($(strip $(RYAZAN_LIBS)),)
CFLAGS += -DUSSR_TLS_OPENSSL $(RYAZAN_CFLAGS)
LDLIBS += $(RYAZAN_LIBS)
else ifneq ($(RYAZAN_REQUIRE_TLS),)
$(error RYAZAN_REQUIRE_TLS=1 requires OpenSSL development files discoverable through pkg-config)
endif
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
       $(SRC_DIR)/ussr_ru.o \
       $(SRC_DIR)/scheduler.o \
       $(PETERSBURG_OBJ) \
       $(SRC_DIR)/eventq.o \
       $(SRC_DIR)/gc.o \
       $(SRC_DIR)/prng64_xrp32.o \
       $(SRC_DIR)/keybind.o \
       $(SRC_DIR)/alias.o \
       $(SRC_DIR)/history.o \
       $(SRC_DIR)/ai.o \
       $(SRC_DIR)/ryazan.o \
       $(SRC_DIR)/ryazan_openssl.o \
       $(SRC_DIR)/ryazan_schannel.o \
       $(SRC_DIR)/help.o \
       $(SRC_DIR)/autocomplete.o		  

ifeq ($(OS),Windows_NT)
OBJS += $(SRC_DIR)/completion_fs_win32.o $(SRC_DIR)/socket_win32.o $(SRC_DIR)/win/process_win32.o
LDLIBS += -lws2_32 -lsecur32 -lcrypt32 -lncrypt -ladvapi32
else
OBJS += $(SRC_DIR)/completion_fs_posix.o $(SRC_DIR)/socket_posix.o $(SRC_DIR)/process_posix.o
endif

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) -lm $(LDLIBS)

# One bison run writes both files; a two-target rule would run it twice
# under make -j, racing on the outputs.
$(SRC_DIR)/parser.tab.c: $(SRC_DIR)/parser.y
	bison -d -Wall -o $(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.y

$(SRC_DIR)/parser.tab.h: $(SRC_DIR)/parser.tab.c

$(SRC_DIR)/lex.yy.c: $(SRC_DIR)/lexer.l $(SRC_DIR)/parser.tab.h
	flex -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/lexer.l

$(SRC_DIR)/parser.tab.o: $(SRC_DIR)/parser.tab.c $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/lex.yy.o: $(SRC_DIR)/lex.yy.c $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr.o: $(SRC_DIR)/ussr.c $(SRC_DIR)/ussr.h $(SRC_DIR)/uno.h $(SRC_DIR)/ussr_oop_builtins.h $(SRC_DIR)/uthash.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr_bytecode.o: $(SRC_DIR)/ussr_bytecode.c $(SRC_DIR)/ussr_bytecode.h $(SRC_DIR)/ussr_uuasm.h $(SRC_DIR)/ussr.h $(SRC_DIR)/vm.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr_ru.o: $(SRC_DIR)/ussr_ru.c $(SRC_DIR)/ussr_ru.h $(SRC_DIR)/ussr_bytecode.h $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr_uuasm.o: $(SRC_DIR)/ussr_uuasm.c $(SRC_DIR)/ussr_uuasm.h $(SRC_DIR)/ussr_bytecode.h $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/autocomplete.o: $(SRC_DIR)/autocomplete.c $(SRC_DIR)/autocomplete.h $(SRC_DIR)/completion_fs.h $(SRC_DIR)/bestline.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/completion_fs_posix.o: $(SRC_DIR)/completion_fs_posix.c $(SRC_DIR)/completion_fs.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/completion_fs_win32.o: $(SRC_DIR)/completion_fs_win32.c $(SRC_DIR)/completion_fs.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/main.o: $(SRC_DIR)/main.c $(SRC_DIR)/ussr.h $(SRC_DIR)/ussr_ru.h $(SRC_DIR)/ussr_bytecode.h $(SRC_DIR)/ussr_uuasm.h $(SRC_DIR)/vm.h $(SRC_DIR)/pp.h $(SRC_DIR)/uno.h $(SRC_DIR)/ussr_oop_builtins.h $(SRC_DIR)/autocomplete.h $(SRC_DIR)/bestline.h $(SRC_DIR)/keybind.h $(SRC_DIR)/alias.h $(SRC_DIR)/history.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/keybind.o: $(SRC_DIR)/keybind.c $(SRC_DIR)/keybind.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ryazan.o: $(SRC_DIR)/ryazan.c $(SRC_DIR)/ryazan.h $(SRC_DIR)/ryazan_backend.h $(SRC_DIR)/socket.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ryazan_openssl.o: $(SRC_DIR)/ryazan_openssl.c $(SRC_DIR)/ryazan_backend.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ryazan_schannel.o: $(SRC_DIR)/ryazan_schannel.c $(SRC_DIR)/ryazan_backend.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ai.o: $(SRC_DIR)/ai.c $(SRC_DIR)/ai.h $(SRC_DIR)/process.h $(SRC_DIR)/ussr_oop_builtins.h $(SRC_DIR)/ryazan.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/alias.o: $(SRC_DIR)/alias.c $(SRC_DIR)/alias.h $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/history.o: $(SRC_DIR)/history.c $(SRC_DIR)/history.h $(SRC_DIR)/ussr_oop_builtins.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/help.o: $(SRC_DIR)/help.c $(SRC_DIR)/help.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/pp.o: $(SRC_DIR)/pp.c $(SRC_DIR)/pp.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/uno.o: $(SRC_DIR)/uno.c $(SRC_DIR)/uno.h $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr_oop_builtins.o: $(SRC_DIR)/ussr_oop_builtins.c $(SRC_DIR)/ussr_oop_builtins.h $(SRC_DIR)/ussr.h $(SRC_DIR)/uno.h $(SRC_DIR)/ai.h $(SRC_DIR)/help.h $(SRC_DIR)/keybind.h $(SRC_DIR)/alias.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/bestline.o: $(SRC_DIR)/bestline.c $(SRC_DIR)/bestline.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/process_posix.o: $(SRC_DIR)/process_posix.c
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/win/process_win32.o: $(SRC_DIR)/win/process_win32.c $(SRC_DIR)/process.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/scheduler.o: $(SRC_DIR)/scheduler.c $(SRC_DIR)/scheduler.h $(SRC_DIR)/petersburg.h $(SRC_DIR)/vm.h $(SRC_DIR)/eventq.h $(SRC_DIR)/gc.h $(SRC_DIR)/socket.h
	$(CC) $(CFLAGS) -c $< -o $@

$(PETERSBURG_OBJ): $(SRC_DIR)/petersburg.c $(SRC_DIR)/petersburg.h $(SRC_DIR)/ttf.h $(SRC_DIR)/scheduler.h $(SRC_DIR)/eventq.h
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
	cp "$(TARGET)" "$(URAL_BIN)" src/ural/ural.su docs/USSR_USER_MANUAL.md moscow.su "$$stage/ussr-linux-bin/"; \
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
