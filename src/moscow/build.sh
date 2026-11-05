#!/bin/sh
# Builds the Moscow disks from the repository root (or anywhere: paths
# are taken from this script):
#
#     sh src/moscow/build.sh [ussr]
#
# build/moscow/moscow.img  the boot image alone: boot.nasm -> boot.bin,
#                          ussr_rt.nasm -> ussr_rt.bin, moscow.su and
#                          russia.su -> .ru with `ussr -c`, put together
#                          by image.nasm
# build/moscow/ussr.img    the install disk: an MBR disk whose first 4 MiB
#                          hold the boot image and whose one partition is
#                          a Belarus file system with the programs (/bin),
#                          the boot files (/boot), /etc and the whole
#                          project (/src/ussr, the files git tracks)
#
# Needs nasm (NASM=path to override) and a ussr binary (first argument,
# else ./ussr or ./ussr.exe at the root).
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
arch=$here/arch/amd64
out=$root/build/moscow

USSR=${1:-}
if [ -z "$USSR" ]; then
    if [ -x "$root/ussr" ]; then USSR=$root/ussr; else USSR=$root/ussr.exe; fi
fi
# the steps below run in other folders: a relative path stays good
case "$USSR" in
    /*|[A-Za-z]:*) ;;
    */*) USSR=$(pwd)/$USSR ;;
esac
NASM=${NASM:-nasm}
if ! command -v "$NASM" >/dev/null 2>&1; then
    for n in "$LOCALAPPDATA/bin/NASM/nasm.exe" "/c/Program Files/NASM/nasm.exe"; do
        [ -x "$n" ] && NASM=$n && break
    done
fi
# Git for Windows rewrites arguments that look like paths ("/bin" becomes
# a folder of Git's): ussr's arguments go through as they are (run_ussr),
# with host paths in Windows form (hp)
run_ussr() {
    MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' USSR_KIEV= "$USSR" "$@"
}
hp() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi
}

mkdir -p "$out"
"$NASM" -f bin -i "$arch/" "$arch/boot.nasm" -o "$out/boot.bin"
"$NASM" -f bin -i "$arch/" "$arch/ussr_rt.nasm" -o "$out/ussr_rt.bin"
# no startup file may change what gets compiled
(cd "$here" && run_ussr -c moscow.su "$(hp "$out/moscow.ru")")
(cd "$here" && run_ussr -c install.su "$(hp "$out/install.ru")")
(cd "$here/../russia" && run_ussr -c russia.su "$(hp "$out/russia.ru")")
(cd "$here/../stalin" && run_ussr -c stalin.su "$(hp "$out/stalin.ru")")
(cd "$here/../belarus" && run_ussr -c belarus.su "$(hp "$out/belarus.ru")")
"$NASM" -f bin -i "$out/" -i "$arch/" "$arch/image.nasm" -o "$out/moscow.img"
echo "build/moscow/moscow.img: $(wc -c < "$out/moscow.img") bytes"

# the install disk
boot=$(wc -c < "$out/moscow.img")
if [ "$boot" -gt $((4 * 1048576)) ]; then
    echo "build.sh: the boot image is over 4 MiB, the room in front of the partition" >&2
    exit 1
fi
img=$out/ussr.img
rm -f "$img"
stalin() { (cd "$root" && run_ussr src/stalin/stalin.su "$@" >/dev/null); }
belarus() { (cd "$root" && run_ussr src/belarus/belarus.su "$@"); }
STALIN_SEED=1917
export STALIN_SEED
stalin mkimg "$(hp "$img")" 64M
stalin init "$(hp "$img")" mbr
stalin add "$(hp "$img")" --start 4M --type belarus --boot on
# the boot image in front of the partition: the MBR's boot code (its
# first 440 bytes; the partition table stays), then sectors 1 ..
dd if="$out/moscow.img" of="$img" bs=440 count=1 conv=notrunc 2>/dev/null
dd if="$out/moscow.img" of="$img" bs=512 skip=1 seek=1 count=$((boot / 512 - 1)) conv=notrunc 2>/dev/null
dev=$(hp "$img")
belarus mkfs "$dev" --part 1 --label ussr-install
for f in ussr_rt.bin moscow.ru russia.ru; do
    belarus put "$dev" "$(hp "$out/$f")" "/boot/$f"
done
for f in install stalin belarus; do
    belarus put "$dev" "$(hp "$out/$f.ru")" "/bin/$f.ru"
done
belarus write "$dev" /etc/hostname "moscow-install"
belarus write "$dev" /etc/motd "USSR install disk: install(_): puts USSR on a disk of its own; help(_): lists the commands."
# Murmansk, the firewall: nothing comes in but ping and the forum's ports
printf '%s\n' "# Murmansk, the firewall: the first rule that matches decides" \
    "policy in deny" "policy out allow" "allow in icmp" \
    "allow in tcp 8080" "allow in tcp 8443" >"$out/murmansk.rules"
belarus put "$dev" "$(hp "$out/murmansk.rules")" /etc/murmansk.rules
# Petersburg's screen (gui.su); an installed system adds desktop=on
belarus write "$dev" /etc/gui.conf "screen=1024x768"
# C libraries for c_load (src/moscow/lib/*.c): freestanding ELF shared
# objects into /lib, when clang and ld.lld are there
CLANG=${CLANG:-clang}
LLD=${LLD:-ld.lld}
for t in "$CLANG" "/c/Program Files/LLVM/bin/clang"; do
    command -v "$t" >/dev/null 2>&1 && CLANG=$t && break
done
for t in "$LLD" "/c/Program Files/LLVM/bin/ld.lld"; do
    command -v "$t" >/dev/null 2>&1 && LLD=$t && break
done
# BearSSL (https://bearssl.org, MIT): TLS for tls.c, fetched once into
# build/moscow, checked against its SHA-256, compiled the same way
BEARSSL=bearssl-0.6
BEARSSL_SHA256=6705bba1714961b41a728dfc5debbe348d2966c117649392f8c8139efc83ff14
CFLAGS_C="--target=x86_64-unknown-elf -ffreestanding -fno-builtin -fPIC -mno-red-zone -fno-stack-protector -fuse-init-array -O2 -I$here/lib/include"
bearssl_objects() {
    src=$out/src_bearssl/$BEARSSL
    if [ ! -f "$src/inc/bearssl.h" ]; then
        mkdir -p "$out/dl" "$out/src_bearssl"
        tgz=$out/dl/$BEARSSL.tar.gz
        if [ ! -f "$tgz" ]; then
            curl -sSL -o "$tgz" "https://bearssl.org/$BEARSSL.tar.gz" || return 1
        fi
        sum=$(sha256sum "$tgz" | cut -d' ' -f1)
        if [ "$sum" != "$BEARSSL_SHA256" ]; then
            echo "build.sh: $BEARSSL.tar.gz has SHA-256 $sum, not $BEARSSL_SHA256" >&2
            rm -f "$tgz"
            return 1
        fi
        tar -xzf "$tgz" -C "$out/src_bearssl" || return 1
    fi
    obj=$out/bearssl-obj
    mkdir -p "$obj"
    n=0
    # (sh functions share their caller's variables: names of their own)
    for bc in $(cd "$src/src" && find . -name '*.c'); do
        o=$(printf '%s' "${bc#./}" | tr '/' '_')
        o=$obj/${o%.c}.o
        if [ ! -f "$o" ]; then
            "$CLANG" $CFLAGS_C -I"$src/inc" -I"$src/src" -c "$src/src/$bc" -o "$o" &
            n=$((n + 1))
            [ $((n % 8)) -eq 0 ] && wait
        fi
    done
    wait
    [ "$(ls "$obj"/*.o | wc -l)" -gt 200 ]
}
if command -v "$CLANG" >/dev/null 2>&1 && command -v "$LLD" >/dev/null 2>&1; then
    mkdir -p "$out/lib"
    for c in "$here"/lib/*.c; do
        name=$(basename "$c" .c)
        extra=
        if [ "$name" = tls ]; then
            if ! bearssl_objects; then
                echo "build.sh: no BearSSL: no /lib/tls.so" >&2
                continue
            fi
            extra="$out/bearssl-obj/*.o"
            "$CLANG" $CFLAGS_C -I"$out/src_bearssl/$BEARSSL/inc" -c "$c" -o "$out/lib/$name.o"
        else
            "$CLANG" $CFLAGS_C -c "$c" -o "$out/lib/$name.o"
        fi
        # $extra is unquoted on purpose: a glob of objects, or nothing
        "$LLD" -shared --hash-style=sysv -o "$out/lib/$name.so" "$out/lib/$name.o" $extra
        belarus put "$dev" "$(hp "$out/lib/$name.so")" "/lib/$name.so"
    done
else
    echo "build.sh: no clang and ld.lld: no C libraries in /lib" >&2
fi
# Linux programs for /bin (arch/amd64/native.inc): ussr, ural and the test
# program, static PIE against musl (https://musl.libc.org, MIT) built with
# -fPIC: a distro's libc.a may not be position independent. What they are
# built from is fetched once into build/moscow and checked against its
# SHA-256. Needs a Linux host with gcc, flex, bison and perl; elsewhere what an
# earlier build left in build/moscow/native goes on the disk, if anything.
MUSL=musl-1.2.5
MUSL_SHA256=a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4
ZLIB=zlib-1.3.2
ZLIB_SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
OPENSSL=openssl-3.5.9
OPENSSL_SHA256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a
CURL=curl-8.23.0
CURL_SHA256=e3de77cfa6367083c776a26647fc9f633b538c06c3baa73d931a252ac8f72b49
SQLITE=sqlite-autoconf-3540100
SQLITE_SHA256=926ef8d3eab93fbf6e06011901273eec076b25594a4e1cdadd50c987f4f6d64e
# Mozilla's root certificates as curl.se extracts them: /etc/ssl/certs.pem
CACERT=cacert-2026-11-05.pem
CACERT_SHA256=427c064216761a1e72a8b30f0d954d08cb6e4435a82091fb865afd20c8497285
native=$out/native
# fetched url file sha256: into build/moscow/dl, checked
fetched() {
    f=$out/dl/$2
    if [ ! -f "$f" ]; then
        curl -fsSL --retry 3 -o "$f" "$1" || return 1
    fi
    sum=$(sha256sum "$f" | cut -d' ' -f1)
    if [ "$sum" != "$3" ]; then
        echo "build.sh: $2 has SHA-256 $sum, not $3" >&2
        rm -f "$f"
        return 1
    fi
}
native_build() {
    [ "$(uname -s)" = Linux ] || return 1
    for t in gcc make flex bison curl; do
        command -v "$t" >/dev/null 2>&1 || return 1
    done
    mkdir -p "$out/dl" "$native"
    prefix=$native/musl
    if [ ! -x "$prefix/bin/musl-gcc" ]; then
        fetched "https://musl.libc.org/releases/$MUSL.tar.gz" "$MUSL.tar.gz" "$MUSL_SHA256" || return 1
        tgz=$out/dl/$MUSL.tar.gz
        rm -rf "$native/musl-src"
        mkdir -p "$native/musl-src"
        tar -xzf "$tgz" -C "$native/musl-src" || return 1
        (cd "$native/musl-src/$MUSL" &&
            ./configure --prefix="$prefix" --syslibdir="$prefix/lib" --disable-shared \
                CFLAGS="-fPIC -O2" >/dev/null &&
            make -j"$(nproc)" >/dev/null 2>&1 && make install >/dev/null 2>&1) || return 1
        # a gcc that links libatomic this way finds nothing it needs in it
        ar rcs "$prefix/lib/libatomic_asneeded.a"
    fi
    musl=$prefix/bin/musl-gcc
    lib=$prefix/lib
    # the flags, not -static-pie: not every gcc takes that from musl's specs
    pie="-nostartfiles -static -Wl,-pie,--no-dynamic-linker,-z,text $lib/rcrt1.o $lib/crti.o"
    # a copy of the sources: the host build's objects share their names
    rm -rf "$native/src"
    mkdir -p "$native/src"
    (cd "$root" && tar -cf - src/*.c src/*.h src/parser.y src/lexer.l ussr-ubuntu.mk configure.ac) |
        tar -xf - -C "$native/src" || return 1
    (cd "$native/src" && make -f ussr-ubuntu.mk -j"$(nproc)" CC="$musl" PETERSBURG_LIBS= RYAZAN_LIBS= \
        CFLAGS="-std=c11 -O2 -fPIE -D_GNU_SOURCE -w" LDFLAGS="$pie" LDLIBS="$lib/crtn.o" ussr \
        >"$native/ussr.log" 2>&1) || { tail -20 "$native/ussr.log" >&2; return 1; }
    strip "$native/src/ussr" 2>/dev/null
    cp "$native/src/ussr" "$native/ussr"
    "$musl" -fPIE -O1 -w $pie -o "$native/moscow_native" "$root/tests/moscow_native.c" "$lib/crtn.o" || return 1
    native_ural
}
# ural and what it links: zlib, OpenSSL, curl, SQLite
native_ural() {
    deps=$native/deps
    mkdir -p "$deps"
    picflags="-fPIC -O2 -D_GNU_SOURCE"
    if [ ! -f "$deps/lib/libz.a" ]; then
        fetched "https://zlib.net/$ZLIB.tar.gz" "$ZLIB.tar.gz" "$ZLIB_SHA256" || return 1
        tar -xzf "$out/dl/$ZLIB.tar.gz" -C "$native" || return 1
        (cd "$native/$ZLIB" && CC="$musl" CFLAGS="$picflags" ./configure --static --prefix="$deps" >/dev/null &&
            make -j"$(nproc)" >/dev/null 2>&1 && make install >/dev/null 2>&1) || return 1
    fi
    if [ ! -f "$deps/lib/libssl.a" ]; then
        fetched "https://github.com/openssl/openssl/releases/download/$OPENSSL/$OPENSSL.tar.gz" \
            "$OPENSSL.tar.gz" "$OPENSSL_SHA256" || return 1
        tar -xzf "$out/dl/$OPENSSL.tar.gz" -C "$native" || return 1
        (cd "$native/$OPENSSL" && CC="$musl" ./Configure linux-x86_64 no-shared no-tests no-docs no-async \
            no-dso no-engine no-afalgeng no-secure-memory no-quic no-dtls no-ssl3 no-legacy no-ui-console \
            no-comp -fPIC -O2 --prefix="$deps" --libdir=lib >/dev/null &&
            make -j"$(nproc)" build_libs >/dev/null 2>&1 && make install_dev >/dev/null 2>&1) || return 1
    fi
    if [ ! -f "$deps/lib/libcurl.a" ]; then
        fetched "https://curl.se/download/$CURL.tar.gz" "$CURL.tar.gz" "$CURL_SHA256" || return 1
        tar -xzf "$out/dl/$CURL.tar.gz" -C "$native" || return 1
        (cd "$native/$CURL" && CC="$musl" CFLAGS="$picflags" CPPFLAGS="-I$deps/include" LDFLAGS="-L$deps/lib" \
            ./configure --prefix="$deps" --disable-shared --enable-static --with-openssl="$deps" \
                --with-zlib="$deps" --disable-threaded-resolver --disable-ipv6 --disable-unix-sockets \
                --disable-ldap --disable-rtsp --disable-dict --disable-telnet --disable-tftp \
                --disable-pop3 --disable-imap --disable-smtp --disable-gopher --disable-mqtt \
                --disable-smb --disable-manual --disable-docs --disable-ntlm --disable-alt-svc \
                --without-libpsl --without-brotli --without-zstd --without-nghttp2 --without-libidn2 \
                --without-ca-bundle --without-ca-path >/dev/null 2>&1 &&
            make -j"$(nproc)" -C lib >/dev/null 2>&1 && make -C lib install >/dev/null 2>&1 &&
            make -C include install >/dev/null 2>&1) || return 1
    fi
    if [ ! -f "$native/$SQLITE/sqlite3.c" ]; then
        fetched "https://www.sqlite.org/2026/$SQLITE.tar.gz" "$SQLITE.tar.gz" "$SQLITE_SHA256" || return 1
        tar -xzf "$out/dl/$SQLITE.tar.gz" -C "$native" || return 1
    fi
    nat=$root/src/ural/native
    "$musl" -std=c11 -O2 -fPIE -w -D_GNU_SOURCE -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION \
        -DCURL_STATICLIB -I"$deps/include" -I"$native/$SQLITE" -I"$nat" $pie -o "$native/ural" \
        "$nat/ural.c" "$nat/ural_db.c" "$nat/ural_archive.c" "$nat/ural_manifest.c" "$nat/ural_url.c" \
        "$native/$SQLITE/sqlite3.c" -L"$deps/lib" -lcurl -lssl -lcrypto -lz -lm "$lib/crtn.o" || return 1
    strip "$native/ural" 2>/dev/null
    return 0
}
native_build || echo "build.sh: Linux programs not built here" >&2
for f in ussr ural moscow_native; do
    if [ -f "$native/$f" ]; then
        belarus put "$dev" "$(hp "$native/$f")" "/bin/$f"
    else
        echo "build.sh: no /bin/$f" >&2
    fi
done
if fetched "https://curl.se/ca/$CACERT" "$CACERT" "$CACERT_SHA256" 2>/dev/null; then
    belarus put "$dev" "$(hp "$out/dl/$CACERT")" /etc/ssl/certs.pem
else
    echo "build.sh: no /etc/ssl/certs.pem" >&2
fi
# the project: the files git tracks (every file in the tree when this is
# no git checkout), never build products - objects, binaries, packages,
# disk images - whichever way they got there
list=$out/files.txt
if git -C "$root" ls-files >"$list.all" 2>/dev/null && [ -s "$list.all" ]; then :; else
    (cd "$root" && find . \( -path ./build -o -path ./.git -o -path ./.claude \
        -o -path ./autom4te.cache \) -prune -o -type f -print | sed 's|^\./||') >"$list.all"
fi
grep -viE '\.(obj|o|a|ural|exe|dll|so|lib|pdb|ilk|exp|res|msi|wixpdb|img|qcow2|iso|ru|tmp|log)$' \
    "$list.all" >"$list"
rm -f "$list.all"
belarus putlist "$dev" "$(hp "$list")" /src/ussr
belarus fsck "$dev"
echo "build/moscow/ussr.img: $(wc -c < "$img") bytes"
