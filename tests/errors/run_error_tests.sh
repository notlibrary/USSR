#!/bin/sh
# Compile-time diagnostics: every bad program must fail with exit status 1,
# print nothing on stdout, and report "file:line: reason" exactly once.
# Run from the repository root:  sh tests/errors/run_error_tests.sh [ussr]
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
pass=0
fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok() { pass=$((pass + 1)); printf 'PASS %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; [ -n "$2" ] && printf '%s\n' "$2"; }

# check NAME WANT [ussr arguments...]: stderr has exactly one line
# containing WANT (a "file:line: reason" fragment), status 1, no stdout.
check() {
    name=$1; want=$2; shift 2
    "$USSR" "$@" </dev/null >"$tmp/out" 2>"$tmp/err"
    status=$?
    hits=$(grep -cF -- "$want" "$tmp/err")
    located=$(grep -cE '^[^ ]+:[0-9]+: ' "$tmp/err")
    if [ "$status" -eq 1 ] && [ ! -s "$tmp/out" ] &&
       [ "$hits" -eq 1 ] && [ "$located" -eq 1 ]; then
        ok "$name"
    else
        bad "$name" "exit $status, want '$want'; stdout: $(cat "$tmp/out"); stderr: $(cat "$tmp/err")"
    fi
}

# native PATH: PATH as ussr.exe sees it inside a file (MSYS translates
# arguments, not file contents)
native() {
    if command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s' "$1"; fi
}

# program NAME: write stdin to $tmp/NAME.su
program() { cat >"$tmp/$1.su"; }

program print <<'EOF'
init(ret): arg_cnt arg_vec [
    set(a): 1
    print(_): 1 2 3
    return(ret): 0
]
EOF
check "too many print values in init" \
    "print.su:3: print expects exactly one value, got 3" "$tmp/print.su"

program get <<'EOF'
# a comment and a blank line first

init(ret): arg_cnt arg_vec [
    set(v): 1
    get(x): v 0
    return(ret): 0
]
EOF
check "get with a value as destination" \
    "get.su:5: get(vector): index destination needs a variable name" \
    "$tmp/get.su"

cat >"$tmp/lib.su" <<'EOF'
# library with a mistake on line 3
helper(r): a [
    add(r): a
]
EOF
program include <<'EOF'
$define LIMIT 10
$include "lib.su"
init(ret): arg_cnt arg_vec [
    helper(r): LIMIT
    return(ret): 0
]
EOF
check "error inside an included file" \
    "lib.su:3: add expects two values, got 1" "$tmp/include.su"

program after_include <<'EOF'
$include "ok_lib.su"
$ifdef NOT_DEFINED
print(_): "skipped"
$endif

print(_): 1 2
EOF
printf 'ok(r): a [\n    set(r): a\n]\n' >"$tmp/ok_lib.su"
check "line numbers survive includes and skipped lines" \
    "after_include.su:6: print expects" "$tmp/after_include.su"

program syntax <<'EOF'
print(_): "x"
print(_: oops
EOF
check "syntax error" "syntax.su:2: syntax error" "$tmp/syntax.su"

program syntax_late <<'EOF'
init(ret): arg_cnt arg_vec [
    set(a): 1


    set(b: 2
]
EOF
check "syntax error after blank lines" \
    "syntax_late.su:5: syntax error" "$tmp/syntax_late.su"

printf 'set(a): 1\nset(b): `\n' >"$tmp/char.su"
check "unexpected character" "char.su:2: unexpected character" "$tmp/char.su"

program block <<'EOF'
set(a): 1
print(_): a [
    set(b): 2
]
EOF
check "block on a command that takes none" \
    "block.su:2: command 'print' cannot have a [] block" "$tmp/block.su"

program arity <<'EOF'
pair(r): a b [
    add(r): a b
]
init(ret): arg_cnt arg_vec [
    pair(x): 1
    return(ret): 0
]
EOF
check "wrong argument count for a function" \
    "arity.su:5: function 'pair' expects 2 parameters, got 1" "$tmp/arity.su"

program dup <<'EOF'
f(r): a [
    set(r): a
]
f(r): b [
    set(r): b
]
EOF
check "function defined twice" "dup.su:4: function 'f' is defined twice" \
    "$tmp/dup.su"

printf 'set(a): 1\nreturn(r): a\n' >"$tmp/ret.su"
check "return outside a function" "ret.su:2: return outside a function" \
    "$tmp/ret.su"

printf 'set(a): 1\nbreak(b): a\n' >"$tmp/brk.su"
check "break outside a loop" "brk.su:2: break outside a loop" "$tmp/brk.su"

program nested <<'EOF'
init(ret): arg_cnt arg_vec [
    set(i): 0
    while(_): {i < 3} [
        if(_): {i = 1} [
            add(i): i
        ]
        add(i): i 1
    ]
    return(ret): 0
]
EOF
check "error in a nested block" "nested.su:5: add expects two values, got 1" \
    "$tmp/nested.su"

program dowhile <<'EOF'
set(i): 0
do(_): 1 [
    add(i): i 1
]
print(_): i
EOF
check "do without loop" \
    "dowhile.su:2: do must be followed by loop(_): condition" "$tmp/dowhile.su"

printf 'set(i): 0\nloop(_): {i < 3}\n' >"$tmp/loop.su"
check "loop without do" "loop.su:2: loop without a do before it" "$tmp/loop.su"

printf 'set(a): 1\nelse(_):\nprint(_): a\n' >"$tmp/else.su"
check "else without if" "else.su:2: else without a matching if" "$tmp/else.su"

program choose <<'EOF'
set(a): 1
choose(_): a
option(_): 1 2
print(_): a
EOF
check "option with two values" "choose.su:3: option expects one value" \
    "$tmp/choose.su"

printf 'f(r): x [\n    global(_): 5\n    set(r): x\n]\n' >"$tmp/glob.su"
check "global with a value" "glob.su:2: global expects variable names" \
    "$tmp/glob.su"

printf 'f(r): x [\n    global(_): x\n    set(r): x\n]\n' >"$tmp/gparam.su"
check "parameter declared global" \
    "gparam.su:1: 'x' is a parameter or the return variable of 'f'" \
    "$tmp/gparam.su"

# warn NAME WANT FILE: compiles and runs (exit 0) with warning WANT.
warn() {
    "$USSR" "$3" </dev/null >"$tmp/out" 2>"$tmp/err"
    status=$?
    if [ "$status" -eq 0 ] && grep -qF -- "$2" "$tmp/err"; then
        ok "$1"
    else
        bad "$1" "exit $status, want '$2'; stderr: $(cat "$tmp/err")"
    fi
}

program state <<'EOF'
show(_): [
    print(_): mode
]
init(ret): arg_cnt arg_vec [
    set(mode): "edit"
    show(_):
    set(ret): 0
]
EOF
warn "state set in init but not declared" \
    "state.su:2: warning: 'mode' is read as a global but only set as a local (in 'init', line 5); declare global(_): mode" \
    "$tmp/state.su"

program counter <<'EOF'
bump(_): [
    print(_): n
    set(n): 1
]
init(ret): arg_cnt arg_vec [
    bump(_):
    set(ret): 0
]
EOF
warn "local read before it is set" \
    "counter.su:2: warning: 'n' is read before it is set in 'bump'" \
    "$tmp/counter.su"

program flag <<'EOF'
parse(r): text [
    set(failed): true
    set(r): text
]
run(r): text [
    set(failed): false
    parse(r): text
    if(_): failed [
        set(r): "error"
    ]
]
init(ret): arg_cnt arg_vec [
    run(x): "a"
    set(ret): 0
]
EOF
warn "flag set for the caller" \
    "flag.su:2: warning: 'failed' is set in 'parse' but never read there, while other code reads it" \
    "$tmp/flag.su"

printf 'set(a): 1\nset(b): 9223372036854775808\n' >"$tmp/bigint.su"
check "integer literal out of range" \
    "bigint.su:2: integer 9223372036854775808 is outside" "$tmp/bigint.su"

printf 'set(a): 0xFF\nset(b): 0x1CAFEBABEDEADBEEF\n' >"$tmp/bighex.su"
check "hex literal over 64 bits" \
    "bighex.su:2: hex integer 0x1CAFEBABEDEADBEEF has more than 16 digits (64 bits)" "$tmp/bighex.su"

# A shift count outside 0..63 stops the program at run time.
printf 'set(n): 64\nprint(_): {1 << n}\n' >"$tmp/shift.su"
"$USSR" "$tmp/shift.su" </dev/null >"$tmp/out" 2>"$tmp/err"
status=$?
[ "$status" -ne 0 ] && grep -qF "shift count 64 is outside 0..63" "$tmp/err" &&
    ok "shift count out of range" ||
    bad "shift count out of range" "exit $status; $(cat "$tmp/err")"

check "compile only (-c) reports the location" \
    "print.su:3: print expects exactly one value" -c "$tmp/print.su" "$tmp/out.ru"
[ ! -f "$tmp/out.ru" ] && ok "-c writes nothing on error" ||
    bad "-c writes nothing on error"

# load and eval run at run time: the error is reported where the loaded
# or evaluated text has it, then the program stops.
printf 'set(a): 1\nset(b): 2\nprint(_): a b\n' >"$tmp/loaded.su"
printf 'load(p): "%s"\n' "$(native "$tmp/loaded.su")" >"$tmp/load.su"
"$USSR" "$tmp/load.su" </dev/null >"$tmp/out" 2>"$tmp/err"
grep -qF "loaded.su:3: print expects exactly one value" "$tmp/err" &&
    ok "error in a loaded file" ||
    bad "error in a loaded file" "$(cat "$tmp/err")"

printf 'eval(r): "set(a): 1\\nprint(_): a a"\n' >"$tmp/eval.su"
"$USSR" "$tmp/eval.su" </dev/null >"$tmp/out" 2>"$tmp/err"
grep -qF "<eval>:2: print expects exactly one value" "$tmp/err" &&
    ok "error in eval text" || bad "error in eval text" "$(cat "$tmp/err")"

# The REPL numbers the lines it reads.
out=$(printf 'set(a): 1\nprint(_): a a\nprint(_): 7\n' | "$USSR" 2>&1)
printf '%s' "$out" | grep -qF "<stdin>:2: print expects exactly one value" &&
    printf '%s' "$out" | grep -qF 'ussr> 7' &&
    ok "REPL reports its input line and keeps going" ||
    bad "REPL reports its input line and keeps going" "$out"

printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
