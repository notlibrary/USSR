CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic
LEX     = flex
YACC    = bison

TARGET  = ussr
SRC_DIR = src

PREFIX  = /usr

SRCS    = $(SRC_DIR)/parser.tab.c \
          $(SRC_DIR)/lex.yy.c \
          $(SRC_DIR)/ussr.c \
          $(SRC_DIR)/bestline.c \
          $(SRC_DIR)/main.c \
          $(SRC_DIR)/pp.c \
          $(SRC_DIR)/uno.c \
          $(SRC_DIR)/ussr_oop_builtins.c \
          $(SRC_DIR)/ussr_bytecode.c \
          $(SRC_DIR)/process_posix.c

all: $(TARGET)

$(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.tab.h: $(SRC_DIR)/parser.y
	$(YACC) -d -Wall -o $(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.y

$(SRC_DIR)/lex.yy.c: $(SRC_DIR)/lexer.l $(SRC_DIR)/parser.tab.h
	$(LEX) -o $(SRC_DIR)/lex.yy.c $(SRC_DIR)/lexer.l

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRCS)

clean:
	rm -f $(TARGET)
	rm -f $(SRC_DIR)/parser.tab.c $(SRC_DIR)/parser.tab.h
	rm -f $(SRC_DIR)/lex.yy.c
	rm -f $(TARGET).tar.gz

install: $(TARGET)
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp $(TARGET) $(DESTDIR)$(PREFIX)/bin/chmod 755 $(DESTDIR)$(PREFIX)/bin/$(TARGET)

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/$(TARGET)

dist: $(TARGET)
	tar -czvf $(TARGET).tar.gz $(TARGET) docs/ussr_user_manual.md moscow.su

.PHONY: all clean install uninstall dist
