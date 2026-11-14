# moscow.mk - the Moscow disks, from the repository root:
#
#     make -f ussr-ubuntu.mk ussr          # the host's ussr, which builds them
#     make -f moscow.mk [-j N] [USSR=./ussr] [STRICT=1] [all|img|iso|boot|clean]
#
# build/moscow/moscow.img  the boot image alone: boot.nasm -> boot.bin,
#                          ussr_rt.nasm -> ussr_rt.bin, moscow.su and
#                          russia.su -> .ru with `ussr -c`, put together by
#                          image.nasm
# build/moscow/ussr.img    the install disk: an MBR disk whose first 4 MiB
#                          hold the boot image and whose one partition is a
#                          Belarus file system with the programs (/bin), the
#                          boot files (/boot), /etc, the C libraries (/lib)
#                          and the whole project (/src/ussr, the files git
#                          tracks)
# build/moscow/ussr.iso    the same files as an ISO 9660 image with Joliet,
#                          so any system reads them under their long names.
#                          It boots: ussr.img is in it as /boot/ussr.img, the
#                          El Torito boot file, and the kernel runs from that
#                          image (arch/amd64/ata.inc)
#
# What a piece needs, and what happens without it:
#   nasm, ussr                 always (NASM= and USSR= override)
#   clang, ld.lld              /lib/ctest.so, gui.so and tls.so (BearSSL,
#                              fetched once); CLANG= and LLD= override
#   Linux, gcc, flex, bison,   /bin/ussr, /bin/ural and /bin/moscow_native:
#   perl, curl                 static PIE against musl, with zlib, OpenSSL,
#                              curl and SQLite, all fetched once
#   clang, lld-link            BOOTX64.EFI, the UEFI loader (efi/loader.c),
#                              and the ISO's EFI system partition image
#   xorriso                    ussr.iso
#   curl                       /etc/ssl/certs.pem, Mozilla's roots
# Without one the disk is made without that piece (what an earlier build
# left in build/moscow goes on it instead, if anything) and a line says so;
# STRICT=1 (a release) makes it an error. Everything fetched is pinned by
# SHA-256 below and kept in build/moscow/dl; what is built from it stays in
# build/moscow, so a second make redoes only what changed. The disk itself
# and the ISO are made again every time: the project's files go on them.

SHELL := /bin/sh
.SHELLFLAGS := -ec
# the makes this one runs (musl, OpenSSL, curl, ussr-ubuntu.mk) get none
# of its command line's variables (USSR=, STRICT=)
MAKEOVERRIDES :=
# no suffix rules (only here, not in the makes below): src/lexer.c is
# flex's output, never lex's from this make
.SUFFIXES:
.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

ROOT := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
HERE := $(ROOT)/src/moscow
ARCH := $(HERE)/arch/amd64
OUT := $(ROOT)/build/moscow
DL := $(OUT)/dl
NATIVE := $(OUT)/native

have = $(shell command -v $(1) >/dev/null 2>&1 && echo yes)

USSR ?= $(firstword $(wildcard $(ROOT)/ussr $(ROOT)/ussr.exe) $(ROOT)/ussr)
override USSR := $(abspath $(USSR))
NASM ?= nasm
CLANG ?= clang
LLD ?= ld.lld
# lld-link, or a distribution's versioned one
LLD_LINK ?= $(firstword $(foreach t,lld-link lld-link-20 lld-link-19 lld-link-18 lld-link-17,$(if $(call have,$(t)),$(t))) lld-link)
XORRISO ?= xorriso
JOBS := $(shell nproc 2>/dev/null || echo 2)

# ussr's arguments as they are (Git for Windows would rewrite "/bin"), no
# startup file
RUN_USSR = MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' USSR_KIEV= $(USSR)
STALIN = cd $(ROOT) && STALIN_SEED=1917 $(RUN_USSR) src/stalin/stalin.su
BELARUS = cd $(ROOT) && $(RUN_USSR) src/belarus/belarus.su

# a piece that cannot be built: a note, or with STRICT=1 an error
define missing
$(if $(STRICT),$(error moscow.mk: $(1)),$(info moscow.mk: $(1)))
endef

# --- pinned sources

BEARSSL := bearssl-0.6
BEARSSL_SHA256 := 6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14
MUSL := musl-1.2.5
MUSL_SHA256 := a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
ZLIB := zlib-1.3.2
ZLIB_SHA256 := bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
OPENSSL := openssl-3.5.9
OPENSSL_SHA256 := 603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
CURL := curl-8.23.0
CURL_SHA256 := e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49
SQLITE := sqlite-autoconf-3540100
SQLITE_SHA256 := 926ef8d3eab93fbf6e06011901273eec076b25594a4e1cdadd50c987f4f6d64e
# Mozilla's root certificates as curl.se extracts them: /etc/ssl/certs.pem
CACERT := cacert-2026-11-05.pem
CACERT_SHA256 := 427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285

# $(call fetch,url,sha256): the target, into build/moscow/dl, checked
define fetch
mkdir -p $(DL)
curl -fsSL --retry 3 -o $@.part '$(1)'
sum=$$(sha256sum $@.part | cut -d' ' -f1); \
if [ "$$sum" != "$(2)" ]; then \
    echo "moscow.mk: $(notdir $@) has SHA-256 $$sum, not $(2)" >&2; rm -f $@.part; exit 1; \
fi
mv $@.part $@
endef

$(DL)/$(BEARSSL).tar.gz:
	$(call fetch,https://bearssl.org/$(BEARSSL).tar.gz,$(BEARSSL_SHA256))
$(DL)/$(MUSL).tar.gz:
	$(call fetch,https://musl.libc.org/releases/$(MUSL).tar.gz,$(MUSL_SHA256))
$(DL)/$(ZLIB).tar.gz:
	$(call fetch,https://zlib.net/fossils/$(ZLIB).tar.gz,$(ZLIB_SHA256))
$(DL)/$(OPENSSL).tar.gz:
	$(call fetch,https://github.com/openssl/openssl/releases/download/$(OPENSSL)/$(OPENSSL).tar.gz,$(OPENSSL_SHA256))
$(DL)/$(CURL).tar.gz:
	$(call fetch,https://curl.se/download/$(CURL).tar.gz,$(CURL_SHA256))
$(DL)/$(SQLITE).tar.gz:
	$(call fetch,https://www.sqlite.org/2026/$(SQLITE).tar.gz,$(SQLITE_SHA256))
$(DL)/$(CACERT):
	$(call fetch,https://curl.se/ca/$(CACERT),$(CACERT_SHA256))

# --- the boot image

ARCH_SRC := $(wildcard $(ARCH)/*.nasm $(ARCH)/*.inc)
# the USSR the .ru files are compiled from: any of the project's .su
SU_SRC := $(wildcard $(ROOT)/src/*.su $(ROOT)/src/*/*.su $(ROOT)/src/*/*/*.su)
RU := moscow install russia stalin belarus

$(OUT)/boot.bin: $(ARCH_SRC)
	mkdir -p $(OUT)
	$(NASM) -f bin -i $(ARCH)/ $(ARCH)/boot.nasm -o $@
$(OUT)/ussr_rt.bin: $(ARCH_SRC)
	mkdir -p $(OUT)
	$(NASM) -f bin -i $(ARCH)/ $(ARCH)/ussr_rt.nasm -o $@

# each compiled in its own folder, as its $include paths are written
$(OUT)/moscow.ru $(OUT)/install.ru: $(OUT)/%.ru: $(SU_SRC) $(USSR)
	mkdir -p $(OUT)
	cd $(HERE) && $(RUN_USSR) -c $*.su $@
$(OUT)/russia.ru $(OUT)/stalin.ru $(OUT)/belarus.ru: $(OUT)/%.ru: $(SU_SRC) $(USSR)
	mkdir -p $(OUT)
	cd $(ROOT)/src/$* && $(RUN_USSR) -c $*.su $@

$(OUT)/moscow.img: $(ARCH_SRC) $(OUT)/boot.bin $(OUT)/ussr_rt.bin $(OUT)/moscow.ru $(OUT)/russia.ru
	$(NASM) -f bin -i $(OUT)/ -i $(ARCH)/ $(ARCH)/image.nasm -o $@
	@size=$$(wc -c < $@); echo "build/moscow/moscow.img: $$size bytes"; \
	if [ "$$size" -gt $$((4 * 1048576)) ]; then \
	    echo "moscow.mk: the boot image is over 4 MiB, the room in front of the partition" >&2; exit 1; \
	fi

boot: $(OUT)/moscow.img

# --- C libraries for c_load (src/moscow/lib/*.c): freestanding ELF shared
# objects for /lib

LIB_NAMES := $(basename $(notdir $(wildcard $(HERE)/lib/*.c)))
CFLAGS_C := --target=x86_64-unknown-elf -ffreestanding -fno-builtin -fPIC -mno-red-zone \
	-fno-stack-protector -fuse-init-array -O2 -I$(HERE)/lib/include
# gui.c takes in lib/gui/*.c and src/petersburg.c
LIB_SRC := $(wildcard $(HERE)/lib/*/*.c $(HERE)/lib/*/*.h $(HERE)/lib/include/*.h \
	$(HERE)/lib/include/*/*.h $(ROOT)/src/petersburg.c $(ROOT)/src/*.h)
BEARSSL_SRC := $(OUT)/src_bearssl/$(BEARSSL)
BEARSSL_OBJ := $(OUT)/bearssl-obj

ifeq ($(call have,$(CLANG))$(call have,$(LLD)),yesyes)
LIBS := $(LIB_NAMES:%=$(OUT)/lib/%.so)

$(BEARSSL_SRC)/inc/bearssl.h: $(DL)/$(BEARSSL).tar.gz
	mkdir -p $(OUT)/src_bearssl
	tar -xzf $< -C $(OUT)/src_bearssl
	touch $@

# every BearSSL source to an object, side by side
$(BEARSSL_OBJ)/.done: $(BEARSSL_SRC)/inc/bearssl.h
	mkdir -p $(BEARSSL_OBJ)
	cd $(BEARSSL_SRC)/src && find . -name '*.c' | sed 's|^\./||' | \
	    xargs -P $(JOBS) -I{} sh -c 'o=$$(printf "%s" "{}" | tr / _); \
	        exec $(CLANG) $(CFLAGS_C) -I$(BEARSSL_SRC)/inc -I$(BEARSSL_SRC)/src -c "{}" \
	        -o "$(BEARSSL_OBJ)/$${o%.c}.o"'
	test "$$(ls $(BEARSSL_OBJ)/*.o | wc -l)" -gt 200
	touch $@

$(OUT)/lib/tls.so: $(HERE)/lib/tls.c $(BEARSSL_OBJ)/.done
	mkdir -p $(OUT)/lib
	$(CLANG) $(CFLAGS_C) -I$(BEARSSL_SRC)/inc -c $< -o $(OUT)/lib/tls.o
	$(LLD) -shared --hash-style=sysv -o $@ $(OUT)/lib/tls.o $(BEARSSL_OBJ)/*.o

$(OUT)/lib/%.so: $(HERE)/lib/%.c $(LIB_SRC)
	mkdir -p $(OUT)/lib
	$(CLANG) $(CFLAGS_C) -c $< -o $(OUT)/lib/$*.o
	$(LLD) -shared --hash-style=sysv -o $@ $(OUT)/lib/$*.o
else
$(call missing,no $(CLANG) and $(LLD): no new C libraries for /lib)
LIBS :=
endif

# --- UEFI: BOOTX64.EFI (src/moscow/efi/loader.c), a PE32+ EFI application,
# and efiboot.img, a FAT12 EFI system partition with it at
# \EFI\BOOT\BOOTX64.EFI (tools/mkesp.su): the ISO's El Torito EFI entry

EFI := $(OUT)/efi/BOOTX64.EFI
ESP_IMG := $(OUT)/efi/efiboot.img
CFLAGS_EFI := --target=x86_64-unknown-windows -ffreestanding -fno-stack-protector \
	-fshort-wchar -mno-red-zone -fno-builtin -O2

ifeq ($(call have,$(CLANG))$(call have,$(LLD_LINK)),yesyes)
$(EFI): $(HERE)/efi/loader.c
	mkdir -p $(OUT)/efi
	$(CLANG) $(CFLAGS_EFI) -c $< -o $(OUT)/efi/loader.obj
	$(LLD_LINK) -subsystem:efi_application -entry:efi_main -nodefaultlib \
	    -out:$@ $(OUT)/efi/loader.obj
else
$(call missing,no $(CLANG) and $(LLD_LINK): no new BOOTX64.EFI)
endif

$(ESP_IMG): $(EFI) $(HERE)/tools/mkesp.su $(USSR)
	rm -f $@
	cd $(ROOT) && $(RUN_USSR) src/moscow/tools/mkesp.su $@ $(EFI)

# --- Linux programs for /bin (arch/amd64/native.inc): ussr, ural and the
# test program, static PIE against musl (https://musl.libc.org, MIT) built
# with -fPIC: a distro's libc.a may not be position independent

NATIVE_TOOLS := gcc make flex bison perl curl
NATIVE_OK := $(if $(filter Linux,$(shell uname -s)),$(if $(strip $(foreach t,$(NATIVE_TOOLS),$(if $(call have,$(t)),,$(t)))),,yes))

ifeq ($(NATIVE_OK),yes)
PROGS := $(NATIVE)/ussr $(NATIVE)/ural $(NATIVE)/moscow_native
PREFIX := $(NATIVE)/musl
MUSL_GCC := $(PREFIX)/bin/musl-gcc
MLIB := $(PREFIX)/lib
DEPS := $(NATIVE)/deps
# the flags, not -static-pie: not every gcc takes that from musl's specs
PIE := -nostartfiles -static -Wl,-pie,--no-dynamic-linker,-z,text $(MLIB)/rcrt1.o $(MLIB)/crti.o
PICFLAGS := -fPIC -O2 -D_GNU_SOURCE

$(MUSL_GCC): $(DL)/$(MUSL).tar.gz
	rm -rf $(NATIVE)/musl-src
	mkdir -p $(NATIVE)/musl-src
	tar -xzf $< -C $(NATIVE)/musl-src
	cd $(NATIVE)/musl-src/$(MUSL) && \
	    ./configure --prefix=$(PREFIX) --syslibdir=$(PREFIX)/lib --disable-shared --enable-wrapper=gcc \
	        CFLAGS="-fPIC -O2" >/dev/null && \
	    $(MAKE) -j$(JOBS) >/dev/null 2>&1 && $(MAKE) install >/dev/null 2>&1
# a gcc that links libatomic this way finds nothing it needs in it
	ar rcs $(MLIB)/libatomic_asneeded.a
	touch $@

$(DEPS)/lib/libz.a: $(DL)/$(ZLIB).tar.gz | $(MUSL_GCC)
	tar -xzf $< -C $(NATIVE)
	cd $(NATIVE)/$(ZLIB) && CC=$(MUSL_GCC) CFLAGS="$(PICFLAGS)" ./configure --static --prefix=$(DEPS) >/dev/null && \
	    $(MAKE) -j$(JOBS) >/dev/null 2>&1 && $(MAKE) install >/dev/null 2>&1

$(DEPS)/lib/libssl.a: $(DL)/$(OPENSSL).tar.gz | $(MUSL_GCC)
	tar -xzf $< -C $(NATIVE)
	cd $(NATIVE)/$(OPENSSL) && CC=$(MUSL_GCC) ./Configure linux-x86_64 no-shared no-tests no-docs no-async \
	    no-dso no-engine no-afalgeng no-secure-memory no-quic no-dtls no-ssl3 no-legacy no-ui-console \
	    no-comp -fPIC -O2 --prefix=$(DEPS) --libdir=lib >/dev/null && \
	    $(MAKE) -j$(JOBS) build_libs >/dev/null 2>&1 && $(MAKE) install_dev >/dev/null 2>&1

$(DEPS)/lib/libcurl.a: $(DL)/$(CURL).tar.gz $(DEPS)/lib/libssl.a $(DEPS)/lib/libz.a
	tar -xzf $< -C $(NATIVE)
# -static: musl here has no dynamic loader, and configure runs what it links
	cd $(NATIVE)/$(CURL) && CC=$(MUSL_GCC) CFLAGS="$(PICFLAGS)" CPPFLAGS="-I$(DEPS)/include" LDFLAGS="-L$(DEPS)/lib -static" \
	    ./configure --prefix=$(DEPS) --disable-shared --enable-static --with-openssl=$(DEPS) \
	        --with-zlib=$(DEPS) --disable-threaded-resolver --disable-ipv6 --disable-unix-sockets \
	        --disable-ldap --disable-rtsp --disable-dict --disable-telnet --disable-tftp \
	        --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt \
	        --disable-smb --disable-manual --disable-docs --disable-ntlm --disable-alt-svc \
	        --without-libpsl --without-brotli --without-zstd --without-nghttp2 --without-libidn2 \
	        --without-ca-bundle --without-ca-path >/dev/null 2>&1 && \
	    $(MAKE) -j$(JOBS) -C lib >/dev/null 2>&1 && $(MAKE) -C lib install >/dev/null 2>&1 && \
	    $(MAKE) -C include install >/dev/null 2>&1

$(NATIVE)/$(SQLITE)/sqlite3.c: $(DL)/$(SQLITE).tar.gz
	mkdir -p $(NATIVE)
	tar -xzf $< -C $(NATIVE)
	touch $@

URAL_SRC := $(addprefix $(ROOT)/src/ural/native/,ural.c ural_db.c ural_archive.c ural_manifest.c ural_url.c)
$(NATIVE)/ural: $(URAL_SRC) $(wildcard $(ROOT)/src/ural/native/*.h) $(NATIVE)/$(SQLITE)/sqlite3.c \
		$(DEPS)/lib/libcurl.a $(DEPS)/lib/libssl.a $(DEPS)/lib/libz.a
	$(MUSL_GCC) -std=c11 -O2 -fPIE -w -D_GNU_SOURCE -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
	    -DCURL_STATICLIB -I$(DEPS)/include -I$(NATIVE)/$(SQLITE) -I$(ROOT)/src/ural/native $(PIE) -o $@ \
	    $(URAL_SRC) $(NATIVE)/$(SQLITE)/sqlite3.c -L$(DEPS)/lib -lcurl -lssl -lcrypto -lz -lm $(MLIB)/crtn.o
	strip $@ 2>/dev/null || true

# ussr links the OpenSSL ural has (Ryazan: tls_*, https:// of prompt), so
# Lenin reaches a model from Moscow. Built in a copy of the sources: the
# host build's objects share their names.
USSR_SRC := $(wildcard $(ROOT)/src/*.c $(ROOT)/src/*.h) $(ROOT)/src/parser.y $(ROOT)/src/lexer.l \
	$(ROOT)/ussr-ubuntu.mk $(ROOT)/configure.ac
$(NATIVE)/ussr: $(USSR_SRC) $(DEPS)/lib/libssl.a | $(MUSL_GCC)
	rm -rf $(NATIVE)/src
	mkdir -p $(NATIVE)/src
	cd $(ROOT) && tar -cf - src/*.c src/*.h src/parser.y src/lexer.l ussr-ubuntu.mk configure.ac | \
	    tar -xf - -C $(NATIVE)/src
	cd $(NATIVE)/src && $(MAKE) -f ussr-ubuntu.mk -j$(JOBS) CC=$(MUSL_GCC) PETERSBURG_LIBS= RYAZAN_LIBS=static \
	    CFLAGS="-std=c11 -O2 -fPIE -D_GNU_SOURCE -w -DUSSR_TLS_OPENSSL -I$(DEPS)/include" LDFLAGS="$(PIE)" \
	    LDLIBS="-L$(DEPS)/lib -lssl -lcrypto $(MLIB)/crtn.o" ussr >$(NATIVE)/ussr.log 2>&1 || \
	    { tail -20 $(NATIVE)/ussr.log >&2; exit 1; }
	strip $(NATIVE)/src/ussr 2>/dev/null || true
	cp $(NATIVE)/src/ussr $@

$(NATIVE)/moscow_native: $(ROOT)/tests/moscow_native.c | $(MUSL_GCC)
	$(MUSL_GCC) -fPIE -O1 -w $(PIE) -o $@ $< $(MLIB)/crtn.o
else
$(call missing,Linux programs need Linux and $(NATIVE_TOOLS): no new /bin/ussr /bin/ural /bin/moscow_native)
PROGS :=
endif

# --- the install disk

MOTD := USSR install disk: install(_): puts USSR on a disk of its own; help(_): lists the commands.
IMG := $(OUT)/ussr.img
LIST := $(OUT)/files.txt
BUILD_PRODUCTS := '\.(obj|o|a|ural|exe|dll|so|lib|pdb|ilk|exp|res|msi|wixpdb|img|qcow2|iso|ru|tmp|log)$$'

# Murmansk, the firewall: nothing comes in but ping and the forum's ports
$(OUT)/murmansk.rules:
	mkdir -p $(OUT)
	printf '%s\n' "# Murmansk, the firewall: the first rule that matches decides" \
	    "policy in deny" "policy out allow" "allow in icmp" \
	    "allow in tcp 8080" "allow in tcp 8443" >$@

# the project: the files git tracks (every file in the tree when this is no
# git checkout), never build products, whichever way they got there
$(LIST): FORCE
	mkdir -p $(OUT)
	if git -C $(ROOT) ls-files >$@.all 2>/dev/null && [ -s $@.all ]; then :; else \
	    cd $(ROOT) && find . \( -path ./build -o -path ./.git -o -path ./.claude \
	        -o -path ./autom4te.cache \) -prune -o -type f -print | sed 's|^\./||' >$@.all; \
	fi
	grep -viE $(BUILD_PRODUCTS) $@.all >$@
	rm -f $@.all

# Mozilla's roots: a release needs them; elsewhere a disk goes without
# them when they cannot be fetched
ifneq ($(STRICT),)
CERTS := $(DL)/$(CACERT)
else
CERTS := certs
certs:
	-@[ -f $(DL)/$(CACERT) ] || $(MAKE) -s -f $(ROOT)/moscow.mk $(DL)/$(CACERT) >/dev/null 2>&1 || \
	    echo "moscow.mk: no /etc/ssl/certs.pem" >&2
endif

$(IMG): $(OUT)/moscow.img $(addprefix $(OUT)/,$(addsuffix .ru,$(RU))) $(OUT)/ussr_rt.bin $(LIBS) $(PROGS) \
		$(OUT)/murmansk.rules $(LIST) $(CERTS) FORCE
	rm -f $@
	$(STALIN) mkimg $@ 64M >/dev/null
	$(STALIN) init $@ mbr >/dev/null
	$(STALIN) add $@ --start 4M --type belarus --boot on >/dev/null
# the boot image in front of the partition: the MBR's boot code (its
# first 440 bytes; the partition table stays), then sectors 1 ..
	boot=$$(wc -c < $(OUT)/moscow.img); \
	dd if=$(OUT)/moscow.img of=$@ bs=440 count=1 conv=notrunc 2>/dev/null; \
	dd if=$(OUT)/moscow.img of=$@ bs=512 skip=1 seek=1 count=$$((boot / 512 - 1)) conv=notrunc 2>/dev/null
	$(BELARUS) mkfs $@ --part 1 --label ussr-install
	for f in ussr_rt.bin moscow.ru russia.ru; do $(BELARUS) put $@ $(OUT)/$$f /boot/$$f; done
	for f in install stalin belarus; do $(BELARUS) put $@ $(OUT)/$$f.ru /bin/$$f.ru; done
	$(BELARUS) write $@ /etc/hostname "moscow-install"
	$(BELARUS) write $@ /etc/motd "$(MOTD)"
	$(BELARUS) put $@ $(OUT)/murmansk.rules /etc/murmansk.rules
# Petersburg's screen (gui.su); an installed system adds desktop=on
	$(BELARUS) write $@ /etc/gui.conf "screen=1024x768"
	for so in $(OUT)/lib/*.so; do \
	    [ -f "$$so" ] || { echo "moscow.mk: no /lib/*.so" >&2; break; }; \
	    $(BELARUS) put $@ $$so /lib/$$(basename $$so); \
	done
	for f in ussr ural moscow_native; do \
	    if [ -f $(NATIVE)/$$f ]; then $(BELARUS) put $@ $(NATIVE)/$$f /bin/$$f; \
	    else echo "moscow.mk: no /bin/$$f" >&2; fi; \
	done
	if [ -f $(DL)/$(CACERT) ]; then $(BELARUS) put $@ $(DL)/$(CACERT) /etc/ssl/certs.pem; fi
	$(BELARUS) putlist $@ $(LIST) /src/ussr
	$(BELARUS) fsck $@
	@echo "build/moscow/ussr.img: $$(wc -c < $@) bytes"

img: $(IMG)

# --- the ISO: the same files as the disk, with Joliet's long names (up to
# 103 characters) and Rock Ridge's

ISO := $(OUT)/ussr.iso
STAGE := $(OUT)/iso

# UEFI's entry when there is a BOOTX64.EFI (built here, or left by an
# earlier build); STRICT=1 wants it
ISO_EFI := $(if $(wildcard $(EFI))$(call have,$(LLD_LINK)),$(ESP_IMG))
ifneq ($(STRICT),)
ISO_EFI := $(ESP_IMG)
endif

ifeq ($(call have,$(XORRISO)),yes)
$(ISO): $(IMG) $(ISO_EFI) FORCE
	rm -rf $@ $(STAGE)
	mkdir -p $(STAGE)/boot $(STAGE)/bin $(STAGE)/etc/ssl $(STAGE)/src/ussr
	for f in ussr_rt.bin moscow.ru russia.ru; do cp $(OUT)/$$f $(STAGE)/boot/$$f; done
	for f in install stalin belarus; do cp $(OUT)/$$f.ru $(STAGE)/bin/$$f.ru; done
	for f in ussr ural moscow_native; do \
	    if [ -f $(NATIVE)/$$f ]; then cp $(NATIVE)/$$f $(STAGE)/bin/$$f; fi; \
	done
	printf '%s' "moscow-install" >$(STAGE)/etc/hostname
	printf '%s' "$(MOTD)" >$(STAGE)/etc/motd
	printf '%s' "screen=1024x768" >$(STAGE)/etc/gui.conf
	cp $(OUT)/murmansk.rules $(STAGE)/etc/murmansk.rules
	if [ -f $(DL)/$(CACERT) ]; then cp $(DL)/$(CACERT) $(STAGE)/etc/ssl/certs.pem; fi
	if ls $(OUT)/lib/*.so >/dev/null 2>&1; then mkdir -p $(STAGE)/lib && cp $(OUT)/lib/*.so $(STAGE)/lib/; fi
	cd $(ROOT) && tar -cf - -T $(LIST) | tar -xf - -C $(STAGE)/src/ussr
# the BIOS loads the boot sector and stage 2: 16 sectors of 512 (TOC_LBA);
# the boot information table is written afterwards (boot_info.sh), as
# xorriso cannot for a boot file this big
# UEFI: a second El Torito entry, the EFI system partition image
	$(XORRISO) -as mkisofs -quiet -iso-level 3 -R -J -joliet-long -V USSR -o $@ \
	    -graft-points -b boot/ussr.img -c boot/boot.catalog -no-emul-boot \
	    -boot-load-size 16 \
	    $(if $(ISO_EFI),-eltorito-alt-boot -e boot/efiboot.img -no-emul-boot) \
	    "boot/ussr.img=$(IMG)" $(if $(ISO_EFI),"boot/efiboot.img=$(ESP_IMG)") "/=$(STAGE)"
	sh $(HERE)/tools/boot_info.sh $@
	@echo "build/moscow/ussr.iso: $$(wc -c < $@) bytes"
iso: $(ISO)
all: $(ISO)
else
$(call missing,no $(XORRISO): no ussr.iso)
iso:
	@echo "moscow.mk: no $(XORRISO): no ussr.iso" >&2; exit 1
all: $(IMG)
endif

# all of build/moscow but what was fetched (dl) and what was built from it
# (BearSSL's objects, musl and the libraries /bin's programs link)
clean:
	rm -rf $(OUT)/*.bin $(OUT)/*.ru $(OUT)/*.img $(OUT)/*.iso $(OUT)/iso $(OUT)/lib \
	    $(OUT)/files.txt $(OUT)/murmansk.rules \
	    $(NATIVE)/ussr $(NATIVE)/ural $(NATIVE)/moscow_native $(NATIVE)/src

FORCE:
.PHONY: all boot img iso clean certs FORCE
