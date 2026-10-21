#!/bin/sh
# .ru bytecode tests: ussr -c compiles, ussr file.ru runs. Round trips
# compare stdout, stderr and exit status with running the source directly;
# malformed files must be rejected before anything runs.
# Run from the repository root:  sh tests/ru/run_ru_tests.sh [ussr]
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
pass=0
fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok() { pass=$((pass + 1)); printf 'PASS %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; [ -n "$2" ] && printf '%s\n' "$2"; }

# roundtrip NAME SOURCE STDIN [ARGS...]
roundtrip() {
    name=$1; src=$2; input=$3; shift 3
    if ! "$USSR" -c "$src" "$tmp/rt.ru" 2>"$tmp/cerr"; then
        bad "$name" "$(cat "$tmp/cerr")"; return
    fi
    printf '%b' "$input" | "$USSR" "$src" "$@" >"$tmp/a.out" 2>"$tmp/a.err"
    a=$?
    printf '%b' "$input" | "$USSR" "$tmp/rt.ru" "$@" >"$tmp/b.out" 2>"$tmp/b.err"
    b=$?
    if [ "$a" = "$b" ] && cmp -s "$tmp/a.out" "$tmp/b.out" &&
       cmp -s "$tmp/a.err" "$tmp/b.err"; then
        ok "$name (exit $a)"
    else
        bad "$name" "exit $a vs $b; diff:
$(diff "$tmp/a.out" "$tmp/b.out"; diff "$tmp/a.err" "$tmp/b.err")"
    fi
}

# rejects NAME FILE MESSAGE: running FILE fails with MESSAGE, no output
rejects() {
    "$USSR" "$2" >"$tmp/r.out" 2>"$tmp/r.err"
    status=$?
    if [ "$status" -ne 0 ] && [ ! -s "$tmp/r.out" ] &&
       grep -qF -- "$3" "$tmp/r.err"; then
        ok "$1"
    else
        bad "$1" "exit $status; stdout: $(cat "$tmp/r.out"); stderr: $(cat "$tmp/r.err")"
    fi
}

# compile_fails NAME MESSAGE ARGS...: -c fails with MESSAGE, writes nothing
compile_fails() {
    name=$1; message=$2; shift 2
    rm -f "$tmp/out.ru"
    "$USSR" -c "$@" >"$tmp/c.out" 2>"$tmp/c.err"
    status=$?
    if [ "$status" -ne 0 ] && [ ! -e "$tmp/out.ru" ] &&
       [ ! -s "$tmp/c.out" ] && grep -qF -- "$message" "$tmp/c.err"; then
        ok "$name"
    else
        bad "$name" "exit $status; stderr: $(cat "$tmp/c.err")"
    fi
}

# patch FILE OFFSET OCTAL: overwrite one byte
patch() {
    printf "\\$3" | dd of="$1" bs=1 seek="$2" conv=notrunc 2>/dev/null
}

# --- round trips ----------------------------------------------------------
roundtrip "language features" tests/ru/roundtrip.su '12 tank\n' alpha beta
roundtrip "matrix multiplication" tests/matrix_mul.su ''
roundtrip "string library" tests/lib_strings.su ''
roundtrip "vectors" tests/vectors/vectors_test.su ''
roundtrip "environment" tests/env_test.su ''
roundtrip "scan from stdin" tests/fac.su '6\n'
roundtrip "async and await" tests/async.su ''
roundtrip "function locals" tests/locals.su ''
roundtrip "load with a bare entry name" tests/boot_load_entry.su ''
roundtrip "runtime error keeps status" tests/vectors/errors/divide_by_zero.su ''

# --- compiling ------------------------------------------------------------
printf 'print(_): "ran"\n' > "$tmp/side.su"
"$USSR" -c "$tmp/side.su" "$tmp/side.ru" >"$tmp/c.out" 2>"$tmp/c.err"
if [ $? -eq 0 ] && [ ! -s "$tmp/c.out" ] && [ ! -s "$tmp/c.err" ]; then
    ok "compiling runs nothing and prints nothing"
else
    bad "compiling runs nothing and prints nothing" "$(cat "$tmp/c.out" "$tmp/c.err")"
fi
out=$("$USSR" "$tmp/side.ru" 2>&1)
[ "$out" = "ran" ] && ok "top-level commands run from .ru" ||
    bad "top-level commands run from .ru" "$out"

"$USSR" -c tests/ru/roundtrip.su "$tmp/one.ru" &&
"$USSR" -c tests/ru/roundtrip.su "$tmp/two.ru" &&
cmp -s "$tmp/one.ru" "$tmp/two.ru" && ok "output is deterministic" ||
    bad "output is deterministic"

head -c 8 "$tmp/one.ru" | od -An -tx1 | tr -d ' \n' > "$tmp/magic"
[ "$(cat "$tmp/magic")" = "89555353520d0a1a" ] &&
    ok "file starts with the signature" || bad "file starts with the signature" "$(cat "$tmp/magic")"

cp tests/fac.su "$tmp/named.su"
"$USSR" -c "$tmp/named.su" && [ -f "$tmp/named.ru" ] &&
    ok "default output name replaces the extension" ||
    bad "default output name replaces the extension"

: > "$tmp/empty.su"
"$USSR" -c "$tmp/empty.su" "$tmp/empty.ru" && "$USSR" "$tmp/empty.ru" &&
    ok "empty program" || bad "empty program"

printf 'print(_): "x"\nprint(_: oops\n' > "$tmp/syntax.su"
compile_fails "syntax error" "syntax error" "$tmp/syntax.su" "$tmp/out.ru"
compile_fails "block argument" "cannot be stored in a .ru file" tests/chain.su "$tmp/out.ru"
compile_fails "missing input" "cannot open" "$tmp/nothing.su" "$tmp/out.ru"
compile_fails "unwritable output" "cannot create" tests/fac.su "$tmp/no/such/dir/out.ru"
compile_fails "input is already bytecode" "already bytecode" "$tmp/one.ru" "$tmp/out.ru"
compile_fails "no input" "Usage"

# --- malformed files ------------------------------------------------------
: > "$tmp/empty.ru"
rejects "empty file" "$tmp/empty.ru" "bad signature"

cp tests/fac.su "$tmp/source.ru"
rejects "source text named .ru" "$tmp/source.ru" "bad signature"

good="$tmp/one.ru"
size=$(wc -c < "$good")

head -c 20 "$good" > "$tmp/short.ru"
rejects "truncated header" "$tmp/short.ru" "truncated"

head -c $((size - 10)) "$good" > "$tmp/cut.ru"
rejects "truncated body" "$tmp/cut.ru" "truncated"

cp "$good" "$tmp/version.ru"; patch "$tmp/version.ru" 8 377
rejects "unknown format version" "$tmp/version.ru" "unsupported .ru format version"

cp "$good" "$tmp/flags.ru"; patch "$tmp/flags.ru" 10 001
rejects "unknown flags" "$tmp/flags.ru" "unsupported .ru flags"

cp "$good" "$tmp/abi.ru"; patch "$tmp/abi.ru" 12 177
rejects "other VM ABI" "$tmp/abi.ru" "ABI mismatch"

cp "$good" "$tmp/crc.ru"; patch "$tmp/crc.ru" 40 377
rejects "corrupted payload" "$tmp/crc.ru" "checksum mismatch"

cp "$good" "$tmp/tail.ru"; printf 'junk' >> "$tmp/tail.ru"
rejects "trailing data" "$tmp/tail.ru" "unexpected data"

rejects "missing file" "$tmp/absent.ru" "cannot open"

printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
