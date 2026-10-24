#!/bin/sh
# REPL prompt tests (PS1/PS2). Commands are piped into the interactive
# interpreter, which reads redirected stdin line by line.
# Run from the repository root:  sh tests/repl/run_repl_tests.sh [ussr]
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
pass=0
fail=0
here=$(basename "$(pwd)")

# expect NAME INPUT WANTED_SUBSTRING [ENV=VALUE ...]
expect() {
    name=$1
    input=$2
    want=$3
    shift 3
    out=$(printf '%b' "$input" | env "$@" "$USSR" 2>&1)
    if printf '%s' "$out" | grep -qF -- "$want"; then
        pass=$((pass + 1)); printf 'PASS %s\n' "$name"
    else
        fail=$((fail + 1)); printf 'FAIL %s\n%s\n' "$name" "$out"
    fi
}

# reject NAME INPUT UNWANTED_LINE [ENV=VALUE ...]  (whole-line match)
reject() {
    name=$1
    input=$2
    unwanted=$3
    shift 3
    out=$(printf '%b' "$input" | env "$@" "$USSR" 2>&1)
    if printf '%s\n' "$out" | grep -qxF -- "$unwanted"; then
        fail=$((fail + 1)); printf 'FAIL %s\n%s\n' "$name" "$out"
    else
        pass=$((pass + 1)); printf 'PASS %s\n' "$name"
    fi
}

expect "default prompt" 'print(_): 1\n' 'ussr> 1' -u PS1
expect "empty PS1 uses the default" 'print(_): 1\n' 'ussr> 1' PS1=
expect "custom PS1" 'print(_): 1\n' 'my> 1' 'PS1=my> '
expect "default continuation prompt" 'while(_): {false} [\nprint(_): 9\n]\nprint(_): 2\n' 'ussr> ... ... ussr> 2' -u PS1 -u PS2
expect "custom PS2" 'while(_): {false} [\nprint(_): 9\n]\n' 'ussr> >> >> ' -u PS1 'PS2=>> '
expect "version escape" 'print(_): 1\n' '[0.4]1' 'PS1=[\v]'
expect "user and host escapes" 'print(_): 1\n' 'tester@box 1' 'PS1=\u@\h ' USERNAME=tester USER=tester COMPUTERNAME=box HOSTNAME=box
expect "directory escape" 'print(_): 1\n' "<$here>1" 'PS1=<\W>'
expect "dollar and backslash escapes" 'print(_): 1\n' '$ \ 1' 'PS1=$ \\ '
expect "unknown escape shown as written" 'print(_): 1\n' '\q>1' 'PS1=\q>'
expect "UTF-8 prompt" 'print(_): 1\n' 'СССР> 1' 'PS1=СССР> '
expect "setenv changes the next prompt" 'setenv(_): "PS1" "new> "\nprint(_): 3\n' 'new> 3' -u PS1
expect "unsetenv restores the default" 'setenv(_): "PS1" "new> "\nunsetenv(_): "PS1"\nprint(_): 4\n' 'ussr> 4' -u PS1
expect "commands still run under a custom prompt" 'set(a): 6\nmul(b): a 7\nprint(_): b\n' '42' 'PS1=x '
reject "PS1 text is never executed" 'print(_): 1\n' 'EXECUTED' 'PS1=print(_): "EXECUTED"'

# A runtime error ends its entry, not the REPL; variables survive it.
# No startup files (USSR_MOSCOW=): ussr numbers a process for each one,
# which russia.su, running inside the host, cannot see.
expect "an error ends the entry, not the REPL" \
    'set(a): 5\nfail(_): "boom"\nprint(_): a\n' \
    'USSR VM: execution failed (status -1)ussr> 5' -u PS1 USSR_MOSCOW=
expect "a failed entry is reported like ussr's" \
    'set(a): 5\nfail(_): "boom"\n' 'process 2 (init) failed' -u PS1 USSR_MOSCOW=
expect "the REPL carries on after several errors" \
    'set(n): {1 / 0}\nfail(_): "again"\nprint(_): "still here"\n' \
    'ussr> still here' -u PS1 USSR_MOSCOW=

printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
