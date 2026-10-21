#!/bin/sh
# Runs every automated USSR test: the suite runners, the standalone scripts
# (checked against tests/expected/), and the GUI tests.
#
# Run from the repository root:
#
#     sh tests/run_all.sh [ussr]            run everything
#     USSR_SKIP_GUI=1 sh tests/run_all.sh   skip tests/gui (no display/GLFW)
#     sh tests/run_all.sh --update [ussr]   rewrite tests/expected/ from the
#                                           current output (review the diff!)
#
# A standalone script's expectation is tests/expected/<path>.txt: its stdout,
# then its stderr and exit status under marker lines. If <path>.in exists
# there it is fed to stdin, otherwise stdin is /dev/null. Carriage returns
# are dropped so Windows and Linux share one expectation.
update=0
if [ "$1" = "--update" ]; then
    update=1
    shift
fi
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
if [ ! -x "$USSR" ]; then
    echo "run_all.sh: no ussr binary (pass its path)" >&2
    exit 2
fi

pass=0
fail=0
failed=
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

ok() { pass=$((pass + 1)); printf 'PASS %s\n' "$1"; }
bad() {
    fail=$((fail + 1)); failed="$failed $1"; printf 'FAIL %s\n' "$1"
    [ -n "$2" ] && printf '%s\n' "$2"
}

# --- suite runners: each prints PASS/FAIL lines and exits nonzero on failure
for runner in tests/ru/run_ru_tests.sh tests/mbl/run_mbl_tests.sh \
              tests/russia/run_uuasm_tests.sh tests/vectors/run_vector_tests.sh \
              tests/repl/run_repl_tests.sh; do
    [ -f "$runner" ] || continue
    [ "$update" = 1 ] && continue
    if sh "$runner" "$USSR" >"$tmp/runner.out" 2>&1 &&
       ! grep -q '^FAIL' "$tmp/runner.out"; then
        ok "$runner ($(tail -n 1 "$tmp/runner.out"))"
    else
        bad "$runner" "$(grep -A 8 '^FAIL' "$tmp/runner.out" | head -n 60)"
    fi
done

# --- standalone scripts against tests/expected/
# Not checked here: network tests (need sockets and a free port), chain.su and
# script.su (output of the host's ps; chaining is POSIX-only), and the quine
# attempts in tests/quines (none of them is a quine yet).
for f in tests/*.su tests/*.ua tests/math/*.su tests/oop/*.su; do
    case "$f" in
        tests/socket_*|tests/ukraine_*|tests/chain.su|tests/script.su) continue ;;
    esac
    want="tests/expected/${f#tests/}.txt"
    input="tests/expected/${f#tests/}.in"
    [ -f "$input" ] || input=/dev/null
    "$USSR" "$f" <"$input" >"$tmp/out" 2>"$tmp/err"
    status=$?
    {
        cat "$tmp/out"
        printf '\n--- stderr ---\n'
        cat "$tmp/err"
        printf '\n--- exit %s ---\n' "$status"
    } | tr -d '\r' >"$tmp/got"
    if [ "$update" = 1 ]; then
        mkdir -p "$(dirname "$want")"
        cp "$tmp/got" "$want"
        continue
    fi
    if [ ! -f "$want" ]; then
        bad "$f" "no expectation: $want (run with --update)"
    elif tr -d '\r' <"$want" | cmp -s "$tmp/got" -; then
        ok "$f"
    else
        bad "$f" "$(tr -d '\r' <"$want" | diff - "$tmp/got" | head -n 40)"
    fi
done

[ "$update" = 1 ] && { echo "tests/expected/ updated"; exit 0; }

# --- GUI tests: need a GLFW-enabled ussr and a display (xvfb-run on Linux)
if [ -n "$USSR_SKIP_GUI" ]; then
    echo "SKIP tests/gui (USSR_SKIP_GUI is set)"
else
    for f in tests/gui/*.su; do
        "$USSR" "$f" </dev/null >"$tmp/gui.out" 2>&1
        status=$?
        if [ "$status" -eq 0 ] && grep -q '^PASS' "$tmp/gui.out" &&
           ! grep -q '^FAIL' "$tmp/gui.out"; then
            ok "$f ($(grep -c '^PASS' "$tmp/gui.out") checks)"
        else
            bad "$f" "exit $status
$(grep -v '^PASS' "$tmp/gui.out" | head -n 30)"
        fi
    done
fi

printf '%d passed, %d failed\n' "$pass" "$fail"
[ -n "$failed" ] && printf 'failed:%s\n' "$failed"
[ "$fail" -eq 0 ]
