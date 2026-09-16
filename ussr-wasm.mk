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
    parser.tab.o \
    lex.yy.o \
    ussr.o \
    ussr_bytecode.o \
    bestline.o \
    main.o \
    pp.o \
    uno.o \
    ussr_oop_builtins.o

.PHONY: all clean dist

all: ussr.wasm

parser.tab.c parser.tab.h: parser.y
	bison -d -Wall parser.y

lex.yy.c: lexer.l parser.tab.h
	flex lexer.l

parser.tab.o: parser.tab.c ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

lex.yy.o: lex.yy.c ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

ussr.o: ussr.c ussr.h uno.h ussr_oop_builtins.h uthash.h
	$(CC) $(CFLAGS) -c $< -o $@

ussr_bytecode.o: ussr_bytecode.c ussr_bytecode.h ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

bestline.o: bestline.c bestline.h
	$(CC) $(CFLAGS) -c $< -o $@

main.o: main.c ussr.h ussr_bytecode.h pp.h uno.h ussr_oop_builtins.h
	$(CC) $(CFLAGS) -c $< -o $@

pp.o: pp.c pp.h
	$(CC) $(CFLAGS) -c $< -o $@

uno.o: uno.c uno.h ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

ussr_oop_builtins.o: ussr_oop_builtins.c ussr_oop_builtins.h ussr.h uno.h
	$(CC) $(CFLAGS) -c $< -o $@

ussr.wasm: $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

clean:
	$(RM) $(OBJECTS) ussr.wasm parser.tab.c parser.tab.h lex.yy.c
	$(RM) -r ussr-wasm-bin ussr-wasm-bin.tar.gz

dist: all
	$(MKDIR) ussr-wasm-bin
	$(CP) ussr.wasm ussr-wasm-bin
	$(CP) README.md CHANGELOG.md LICENSE ussr-wasm-bin 2>/dev/null || true
	tar -czf ussr-wasm-bin.tar.gz ussr-wasm-bin
