CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic
TARGET  = ussr
PREFIX  = /usr
SRC_DIR = src

# Базовые объектные файлы из вашей структуры src/
OBJS = $(SRC_DIR)/parser.tab.o \
       $(SRC_DIR)/lex.yy.o \
       $(SRC_DIR)/ussr.o \
       $(SRC_DIR)/bestline.o \
       $(SRC_DIR)/main.o \
       $(SRC_DIR)/pp.o \
       $(SRC_DIR)/uno.o \
       $(SRC_DIR)/ussr_oop_builtins.o \
       $(SRC_DIR)/ussr_bytecode.o \
       $(SRC_DIR)/process_posix.o \
       $(SRC_DIR)/prng64_xrp32.o \
       $(SRC_DIR)/autocomplete.o		  

# Динамическое добавление файлов в зависимости от ОС
ifeq ($(OS),Windows_NT)
OBJS += $(SRC_DIR)/completion_fs_win32.o
else
OBJS += $(SRC_DIR)/completion_fs_posix.o
endif

all: $(TARGET)

# Линковка финального бинарника
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

# Генерация парсера и лексера
$(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.tab.h: $(SRC_DIR)/parser.y
	bison -d -Wall -o $(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.y

$(SRC_DIR)/lex.yy.c: $(SRC_DIR)/lexer.l $(SRC_DIR)/parser.tab.h
	flex -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/lexer.l

# Правила компиляции отдельных модулей из папки src/
$(SRC_DIR)/parser.tab.o: $(SRC_DIR)/parser.tab.c $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/lex.yy.o: $(SRC_DIR)/lex.yy.c $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr.o: $(SRC_DIR)/ussr.c $(SRC_DIR)/ussr.h $(SRC_DIR)/uno.h $(SRC_DIR)/ussr_oop_builtins.h $(SRC_DIR)/uthash.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/ussr_bytecode.o: $(SRC_DIR)/ussr_bytecode.c $(SRC_DIR)/ussr_bytecode.h $(SRC_DIR)/ussr.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/autocomplete.o: $(SRC_DIR)/autocomplete.c $(SRC_DIR)/autocomplete.h $(SRC_DIR)/completion_fs.h $(SRC_DIR)/bestline.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/completion_fs_posix.o: $(SRC_DIR)/completion_fs_posix.c $(SRC_DIR)/completion_fs.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/completion_fs_win32.o: $(SRC_DIR)/completion_fs_win32.c $(SRC_DIR)/completion_fs.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRC_DIR)/main.o: $(SRC_DIR)/main.c $(SRC_DIR)/ussr.h $(SRC_DIR)/ussr_bytecode.h $(SRC_DIR)/pp.h $(SRC_DIR)/uno.h $(SRC_DIR)/ussr_oop_builtins.h $(SRC_DIR)/autocomplete.h $(SRC_DIR)/bestline.h
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

$(SRC_DIR)/prng64_xrp32.o: $(SRC_DIR)/prng64_xrp32.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(SRC_DIR)/*.o $(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.tab.h $(SRC_DIR)/lex.yy.c $(TARGET).tar.gz

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

dist: $(TARGET)
	tar -czvf $(TARGET).tar.gz $(TARGET) docs/ussr_user_manual.md moscow.su

.PHONY: all clean install uninstall dist
