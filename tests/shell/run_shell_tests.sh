#!/bin/sh
# Shell conveniences: startup files (moscow.su in the home folder and the
# current folder, USSR_MOSCOW) and command aliases.
# Run from the repository root:  sh tests/shell/run_shell_tests.sh [ussr]
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
# The tests change folder: make the binary (and, under the russia.su
# wrapper, its host) absolute paths.
case "$USSR" in
    /*) ;;
    *) USSR=$(pwd)/$USSR ;;
esac
case "${USSR_HOST:-}" in
    ""|/*) ;;
    *) USSR_HOST=$(pwd)/$USSR_HOST; export USSR_HOST ;;
esac
pass=0
fail=0
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok() { pass=$((pass + 1)); printf 'PASS %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s\n' "$1"; [ -n "$2" ] && printf '%s\n' "$2"; }

# expect NAME WANT GOT: compare outputs (carriage returns dropped)
expect() {
    want=$(printf '%s' "$2" | tr -d '\r')
    got=$(printf '%s' "$3" | tr -d '\r')
    if [ "$got" = "$want" ]; then
        ok "$1"
    else
        bad "$1" "want:
$want
got:
$got"
    fi
}

home=$tmp/home
work=$tmp/work
mkdir -p "$home" "$work"
unset USSR_MOSCOW
export HOME="$home"

# --- startup files --------------------------------------------------------

printf 'print(_): "home moscow"\n' >"$home/moscow.su"
printf 'print(_): "work moscow"\n' >"$work/moscow.su"
printf 'print(_): "script"\n' >"$work/s.su"

out=$(cd "$work" && "$USSR" s.su </dev/null 2>&1)
expect "home moscow.su runs, then the current folder's" \
"home moscow
work moscow
script" "$out"

out=$(cd "$home" && "$USSR" "$work/s.su" </dev/null 2>&1)
expect "the home file runs once when it is the current folder's" \
"home moscow
script" "$out"

rm "$work/moscow.su"
out=$(cd "$work" && "$USSR" s.su </dev/null 2>&1)
expect "home moscow.su alone" \
"home moscow
script" "$out"

printf 'print(_): "chosen"\n' >"$tmp/chosen.su"
printf 'print(_): "work moscow"\n' >"$work/moscow.su"
out=$(cd "$work" && USSR_MOSCOW="$tmp/chosen.su" "$USSR" s.su </dev/null 2>&1)
expect "USSR_MOSCOW names the only startup file" \
"chosen
script" "$out"

out=$(cd "$work" && USSR_MOSCOW= "$USSR" s.su </dev/null 2>&1)
expect "empty USSR_MOSCOW loads none" "script" "$out"

out=$(cd "$work" && USSR_MOSCOW="$tmp/missing.su" "$USSR" s.su </dev/null 2>&1)
status=$?
# (MSYS may rewrite the /tmp path ussr prints, so match the file name)
case "$out" in
    "USSR: USSR_MOSCOW: cannot read "*missing.su) ok "unreadable USSR_MOSCOW is an error" ;;
    *) bad "unreadable USSR_MOSCOW is an error" "$out" ;;
esac
expect "unreadable USSR_MOSCOW exits 1" 1 "$status"

printf 'fail(_): "bad home"\n' >"$home/moscow.su"
out=$(cd "$work" && "$USSR" s.su </dev/null 2>&1)
status=$?
case "$out" in
    *"bad home"*work*|*script*) bad "a failing home file stops ussr" "$out" ;;
    *"bad home"*) ok "a failing home file stops ussr" ;;
    *) bad "a failing home file stops ussr" "$out" ;;
esac
expect "a failing home file sets the exit status" 1 "$status"

case "$(uname -s 2>/dev/null)" in
    MINGW*|MSYS*|CYGWIN*)
        printf 'print(_): "profile moscow"\n' >"$home/moscow.su"
        out=$(cd "$work" && env -u HOME USERPROFILE="$home" "$USSR" s.su \
              </dev/null 2>&1)
        expect "Windows: USERPROFILE when HOME is unset" \
"profile moscow
work moscow
script" "$out"
        ;;
esac
rm -f "$home/moscow.su" "$work/moscow.su"

# --- aliases ----------------------------------------------------------------

# As in an interactive shell, aliases apply to typed REPL lines only; they
# usually come from moscow.su. Output follows the prompt on the same line,
# so keep just the output.
repl() {
    (cd "$work" && printf '%s\n' "$@" | "$USSR" 2>&1) | tr -d '\r' |
        sed '1,4d; s/ussr> //g; s/\.\.\. //g; s/USSR VM: execution failed (status -1)//' |
        sed '/^$/d'
}

cat >"$home/moscow.su" <<'EOF'
alias(_): "say" "print"
alias(_): "shout" "say"
alias(_): "hello" "echo" "hello"
alias(_): "dbl" "twice"
EOF
out=$(repl \
    'say(_): "builtin target"' \
    'shout(_): "alias chain"' \
    'hello(_): "world"' \
    'twice(r): x [ mul(r): x 2 ] dbl(v): 21 print(_): v')
expect "REPL with moscow.su aliases: builtin, chain, program, function" \
"builtin target
alias chain
hello world
42" "$out"

printf 'say(_): "from a script"\n' >"$work/script.su"
out=$(cd "$work" && "$USSR" script.su </dev/null 2>&1)
status=$?
case "$status:$out" in
    0:*|*"from a script"*) bad "scripts never see aliases" "$out" ;;
    *) ok "scripts never see aliases" ;;
esac

# (each error case gets a session of its own: russia.su's REPL ends at
# the first runtime error, where ussr's carries on)
out=$(repl 'eval(_): "say(_): \"in eval\""')
case "$out" in
    *"in eval"*) bad "eval inside a REPL line does not expand" "$out" ;;
    *) ok "eval inside a REPL line does not expand" ;;
esac
rm "$home/moscow.su"

out=$(repl \
    'alias(_): "ll" "echo" "[ll]"' \
    'll(_): "a" "b"' \
    'alias(_): "echo" "echo" "[self]"' \
    'echo(_): "once"' \
    './echo(_): "explicit path"')
case "$out" in
    "[ll] a b
[self] once"*) ok "REPL: program with words, self-reference expands once" ;;
    *) bad "REPL: program with words, self-reference expands once" "$out" ;;
esac

out=$(repl \
    'alias(_): "p" "print" "x"' \
    'alias(w): "p"' \
    'len(n): w' \
    'print(_): n' \
    'alias(n):' \
    'alias(_): "p" ""' \
    'alias(w): "p"' \
    'print(_): w')
expect "REPL: read back, list, remove" \
"2
alias(_): \"p\" \"print\" \"x\"
null" "$out"

out=$(repl 'alias(_): "if" "print"'; repl 'alias(_): "9x" "print"')
out=$(printf '%s
' "$out" | grep -c 'cannot be an alias\|is not a command name')
expect "reserved and malformed names are refused" 2 "$out"

printf '%s passed, %s failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
