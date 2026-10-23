#!/bin/sh
# Runs every automated USSR test: the suite runners, the standalone scripts
# (checked against tests/expected/), and the GUI tests. It runs them twice,
# side by side: once with the ussr binary and once with the self-hosted
# interpreter src/russia/russia.su (through tests/russia/ussr-russia), which
# must behave exactly like ussr. Every russia failure is a parity gap.
#
# Run from the repository root:
#
#     sh tests/run_all.sh [ussr]            run everything, ussr then russia
#     sh tests/run_all.sh --russia [ussr]   run only the russia.su pass
#     sh tests/run_all.sh --native [ussr]   run only the ussr pass
#     USSR_SKIP_GUI=1 sh tests/run_all.sh   skip tests/gui (no display/GLFW)
#     sh tests/run_all.sh --update [ussr]   rewrite tests/expected/ from the
#                                           current ussr output (review the
#                                           diff!); russia is not run
#
# The exit status reflects the ussr pass only, so CI stays green while
# russia.su catches up; set USSR_RUSSIA_STRICT=1 to fail on russia gaps too.
# USSR_RUSSIA_TIMEOUT (seconds, default 30) bounds each russia.su run.
#
# A standalone script's expectation is tests/expected/<path>.txt: its stdout,
# then its stderr and exit status under marker lines. If <path>.in exists
# there it is fed to stdin, otherwise stdin is /dev/null. Carriage returns
# are dropped so Windows and Linux share one expectation.
update=0
passes="native russia"
case "$1" in
    --update) update=1; passes=native; shift ;;
    --russia) passes=russia; shift ;;
    --native) passes=native; shift ;;
esac
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
if [ ! -x "$USSR" ]; then
    echo "run_all.sh: no ussr binary (pass its path)" >&2
    exit 2
fi
# The wrapper finds the host binary through USSR_HOST; the suite runners
# inherit it, so they need nothing russia-specific.
USSR_HOST=$USSR
export USSR_HOST
RUSSIA=tests/russia/ussr-russia

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

# suite LABEL USSR: runs every test with USSR as the interpreter, printing
# "PASS|FAIL LABEL test"; sets pass, fail and failed for that label.
suite() {
    label=$1
    ussr=$2
    pass=0
    fail=0
    failed=
    ok() { pass=$((pass + 1)); printf 'PASS %s %s\n' "$label" "$1"; }
    bad() {
        fail=$((fail + 1)); failed="$failed $1"
        printf 'FAIL %s %s\n' "$label" "$1"
        [ -n "$2" ] && printf '%s\n' "$2"
    }

    # --- suite runners: each prints PASS/FAIL lines and exits nonzero on
    # failure. The UUASM runner already compares russia with the C VM.
    for runner in tests/ru/run_ru_tests.sh tests/mbl/run_mbl_tests.sh \
                  tests/russia/run_uuasm_tests.sh tests/vectors/run_vector_tests.sh \
                  tests/repl/run_repl_tests.sh tests/errors/run_error_tests.sh \
                  tests/exit/run_exit_tests.sh; do
        [ -f "$runner" ] || continue
        [ "$update" = 1 ] && continue
        [ "$label" = russia ] && [ "$runner" = tests/russia/run_uuasm_tests.sh ] &&
            continue
        if sh "$runner" "$ussr" >"$tmp/runner.out" 2>&1 &&
           ! grep -q '^FAIL' "$tmp/runner.out"; then
            ok "$runner ($(tail -n 1 "$tmp/runner.out"))"
        else
            bad "$runner" "$(grep -A 8 '^FAIL' "$tmp/runner.out" | head -n 60)
$(tail -n 1 "$tmp/runner.out")"
        fi
    done

    # --- standalone scripts against tests/expected/
    # Not checked here: network tests (need sockets and a free port), chain.su
    # and script.su (output of the host's ps; chaining is POSIX-only), and the
    # quine attempts in tests/quines (none of them is a quine yet).
    for f in tests/*.su tests/*.ua tests/math/*.su tests/oop/*.su; do
        case "$f" in
            tests/socket_*|tests/ukraine_*|tests/chain.su|tests/script.su) continue ;;
        esac
        want="tests/expected/${f#tests/}.txt"
        input="tests/expected/${f#tests/}.in"
        [ -f "$input" ] || input=/dev/null
        "$ussr" "$f" <"$input" >"$tmp/out" 2>"$tmp/err"
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

    [ "$update" = 1 ] && return

    # --- GUI tests: need a GLFW-enabled ussr and a display (xvfb-run on Linux)
    if [ -n "$USSR_SKIP_GUI" ]; then
        echo "SKIP $label tests/gui (USSR_SKIP_GUI is set)"
    else
        for f in tests/gui/*.su; do
            "$ussr" "$f" </dev/null >"$tmp/gui.out" 2>&1
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
}

status=0
summary=
for p in $passes; do
    if [ "$p" = native ]; then
        echo "=== ussr ($USSR)"
        suite ussr "$USSR"
        [ "$fail" -eq 0 ] || status=1
    else
        echo "=== russia.su (on $USSR)"
        suite russia "$RUSSIA"
        [ "$fail" -eq 0 ] || [ -z "$USSR_RUSSIA_STRICT" ] || status=1
    fi
    [ "$update" = 1 ] && { echo "tests/expected/ updated"; exit 0; }
    line=$(printf '%s: %d passed, %d failed' "$label" "$pass" "$fail")
    [ -n "$failed" ] && line="$line
$label failed:$failed"
    summary="$summary$line
"
    # Make the parity gap visible on the GitHub Actions run page.
    if [ -n "$GITHUB_STEP_SUMMARY" ]; then
        {
            printf '### %s: %d passed, %d failed\n\n' "$label" "$pass" "$fail"
            for t in $failed; do printf -- '- `%s`\n' "$t"; done
            printf '\n'
        } >>"$GITHUB_STEP_SUMMARY"
    fi
done

printf '%s' "$summary"
exit $status
