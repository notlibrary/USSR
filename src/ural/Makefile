CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
SQLITE_CFLAGS ?= $(shell pkg-config --cflags sqlite3 2>/dev/null)
SQLITE_LIBS ?= $(shell pkg-config --libs sqlite3 2>/dev/null || echo -lsqlite3)

.PHONY: all test clean
all: test_db

test_db: native/ural_db.c native/ural_db.h tests/test_db.c
	$(CC) $(CFLAGS) $(SQLITE_CFLAGS) -o $@ native/ural_db.c tests/test_db.c $(SQLITE_LIBS)

test: test_db
	./test_db

clean:
	rm -f test_db ural-test-boxes.sqlite
