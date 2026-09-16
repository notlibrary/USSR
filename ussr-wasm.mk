# USSR manual WASM release build Makefile
# Based on the TPM WASI SDK build layout.

WASI_SDK_PATH ?= /opt/wasi-sdk

CC      = $(WASI_SDK_PATH)/bin/clang
RM      = rm -f
CP      = cp -f
MKDIR   = mkdir -p

CFLAGS = \
    --sysroot=$(WASI_SDK_PATH)/share/wasi-sysroot \
    --target=wasm32-wasip1 \
    -O2 \
    -Wall \
    -Wextra

LDFLAGS = \
    --sysroot=$(WASI_SDK_PATH)/share/wasi-sysroot \
    --target=wasm32-wasip1

OBJECTS = \
    src/parser.tab.o \
    src/lex.yy.o \
    src/ussr.o \
    src/ussr_bytecode.o \
    src/bestline.o \
    src/main.o \
    src/pp.o \
    src/uno.o \
    src/ussr_oop_builtins.o

.PHONY: all clean dist

all: ussr.wasm

src/parser.tab.c src/parser.tab.h: src/parser.y
	bison -d -Wall src/parser.y -o src/parser.tab.c

src/lex.yy.c: src/lexer.l src/parser.tab.h
	flex -o src/lex.yy.c src/lexer.l

src/parser.tab.o: src/parser.tab.c src/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

src/lex.yy.o: src/lex.yy.c src/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

src/ussr.o: src/ussr.c src/ussr.h src/uno.h src/ussr_oop_builtins.h src/uthash.h
	$(CC) $(CFLAGS) -c $< -o $@

src/ussr_bytecode.o: src/ussr_bytecode.c src/ussr_bytecode.h src/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

src/bestline.o: src/bestline.c src/bestline.h
	$(CC) $(CFLAGS) -c $< -o $@

src/main.o: src/main.c src/ussr.h src/ussr_bytecode.h src/pp.h src/uno.h src/ussr_oop_builtins.h
	$(CC) $(CFLAGS) -c $< -o $@

src/pp.o: src/pp.c src/pp.h
	$(CC) $(CFLAGS) -c $< -o $@

src/uno.o: src/uno.c src/uno.h src/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

src/ussr_oop_builtins.o: src/ussr_oop_builtins.c src/ussr_oop_builtins.h src/ussr.h src/uno.h
	$(CC) $(CFLAGS) -c $< -o $@

ussr.wasm: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

clean:
	$(RM) $(OBJECTS) ussr.wasm src/parser.tab.c src/parser.tab.h src/lex.yy.c
	$(RM) -r ussr-wasm-bin ussr-wasm-bin.tar.gz

dist: all
	$(MKDIR) ussr-wasm-bin
	$(CP) ussr.wasm ussr-wasm-bin
	$(CP) README.md CHANGELOG.md LICENSE ussr-wasm-bin 2>/dev/null || true
	tar -czf ussr-wasm-bin.tar.gz ussr-wasm-bin
