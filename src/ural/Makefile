CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic
PKG_CFLAGS := $(shell pkg-config --cflags sqlite3 libcurl openssl zlib 2>/dev/null)
PKG_LIBS := $(shell pkg-config --libs sqlite3 libcurl openssl zlib 2>/dev/null)
ifeq ($(strip $(PKG_LIBS)),)
PKG_LIBS = -lsqlite3 -lcurl -lssl -lcrypto -lz
endif

NATIVE = native
TESTS = tests

.PHONY: all test clean
all: ural

ural: $(NATIVE)/ural.c $(NATIVE)/ural_db.c $(NATIVE)/ural_db.h \
      $(NATIVE)/ural_archive.c $(NATIVE)/ural_archive.h \
      $(NATIVE)/ural_manifest.c $(NATIVE)/ural_manifest.h \
      $(NATIVE)/ural_url.c $(NATIVE)/ural_url.h \
      $(NATIVE)/ural_more.c $(NATIVE)/ural_more.h $(NATIVE)/ural_cmds.inc
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -o $@ \
	  $(NATIVE)/ural.c $(NATIVE)/ural_db.c \
	  $(NATIVE)/ural_archive.c $(NATIVE)/ural_manifest.c \
	  $(NATIVE)/ural_url.c $(NATIVE)/ural_more.c $(PKG_LIBS)

test_db: $(NATIVE)/ural_db.c $(NATIVE)/ural_db.h $(TESTS)/test_db.c
	$(CC) $(CFLAGS) $(shell pkg-config --cflags sqlite3 2>/dev/null) \
	  -o $@ $(NATIVE)/ural_db.c $(TESTS)/test_db.c -lsqlite3

test_archive: $(NATIVE)/ural_archive.c $(NATIVE)/ural_archive.h $(TESTS)/test_archive.c
	$(CC) $(CFLAGS) -o $@ $(NATIVE)/ural_archive.c $(TESTS)/test_archive.c -lz

test_manifest: $(NATIVE)/ural_manifest.c $(NATIVE)/ural_manifest.h $(NATIVE)/ural_more.c $(TESTS)/test_manifest.c
	$(CC) $(CFLAGS) -o $@ $(NATIVE)/ural_manifest.c $(NATIVE)/ural_more.c $(TESTS)/test_manifest.c -lcrypto

test_more: $(NATIVE)/ural_more.c $(NATIVE)/ural_more.h $(NATIVE)/ural_manifest.c $(TESTS)/test_more.c
	$(CC) $(CFLAGS) -o $@ $(NATIVE)/ural_more.c $(NATIVE)/ural_manifest.c $(TESTS)/test_more.c -lcrypto

test_url: $(NATIVE)/ural_url.c $(NATIVE)/ural_url.h $(TESTS)/test_url.c
	$(CC) $(CFLAGS) -o $@ $(NATIVE)/ural_url.c $(TESTS)/test_url.c

test: test_db test_archive test_manifest test_url test_more
	./test_db
	./test_archive
	./test_manifest
	./test_url
	./test_more

clean:
	rm -f ural test_db test_archive test_manifest test_url test_more ural-test-boxes.sqlite
