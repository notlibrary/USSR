CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic
LEX     = flex
YACC    = bison

TARGET  = ussr

all: $(TARGET)

parser.tab.c parser.tab.h: parser.y
	$(YACC) -d -Wall parser.y

lex.yy.c: lexer.l parser.tab.h
	$(LEX) lexer.l

$(TARGET): parser.tab.c lex.yy.c ussr.c main.c
	$(CC) $(CFLAGS) -o $(TARGET) \
		parser.tab.c \
		lex.yy.c \
		ussr.c \
		bestline.c \
		main.c \
		pp.c

clean:
	rm -f $(TARGET)
	rm -f parser.tab.c parser.tab.h
	rm -f lex.yy.c

.PHONY: all clean