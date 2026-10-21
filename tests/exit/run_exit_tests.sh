#!/bin/sh
# Exit status: init's integer result is the process exit status (0..255);
# any other integer, a runtime error and a compile error give 1.
# Run from the repository root:  sh tests/exit/run_exit_tests.sh [ussr]
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
pass=0
fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok() { pass=$((pass + 1)); printf 'PASS %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; [ -n "$2" ] && printf '%s\n' "$2"; }

# status NAME WANT [ussr arguments...]
status() {
    name=$1; want=$2; shift 2
    "$USSR" "$@" </dev/null >/dev/null 2>"$tmp/err"
    got=$?
    if [ "$got" -eq "$want" ]; then
        ok "$name (exit $got)"
    else
        bad "$name" "exit $got, want $want; stderr: $(cat "$tmp/err")"
    fi
}

# returns NAME VALUE WANT: init sets ret to VALUE
returns() {
    printf 'init(ret): arg_cnt arg_vec [\n    set(ret): %s\n]\n' "$2" \
        >"$tmp/r.su"
    status "$1" "$3" "$tmp/r.su"
    if "$USSR" -c "$tmp/r.su" "$tmp/r.ru" 2>/dev/null; then
        status "$1, from .ru" "$3" "$tmp/r.ru"
    else
        bad "$1, from .ru" "-c failed"
    fi
}

returns "init returns 0" 0 0
returns "init returns 3" 3 3
returns "init returns 255" 255 255
returns "256 is out of range" 256 1
returns "negative is out of range" "{0 - 1}" 1
returns "a large value is out of range" 5000000000 1
returns "a string result is success" '"done"' 0
returns "a null result is success" null 0

printf 'init(ret): arg_cnt arg_vec [\n    print(_): "no ret"\n]\n' >"$tmp/none.su"
status "init without ret" 0 "$tmp/none.su"

printf 'print(_): "top level"\n' >"$tmp/top.su"
status "program without init" 0 "$tmp/top.su"

printf 'init(ret): arg_cnt arg_vec [\n    print(_): {1 / 0}\n    set(ret): 7\n]\n' \
    >"$tmp/crash.su"
status "runtime error" 1 "$tmp/crash.su"

printf 'init(ret): arg_cnt arg_vec [\n    print(_): 1 2\n]\n' >"$tmp/bad.su"
status "compile error" 1 "$tmp/bad.su"

status "-e error" 1 -e 'print(_): 1 2'
status "-e init result" 9 -e 'init(ret): arg_cnt arg_vec [
    set(ret): 9
]'

printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
