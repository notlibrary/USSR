#!/bin/sh
# UUASM conformance tests for the russia.su bootstrap against the C VM.
# Run from the repository root:  sh tests/russia/run_uuasm_tests.sh [ussr]
#
#   uuasm/*.ua               both runtimes print <name>.expect; russia's
#                            "dump" output, run by the C VM, prints it too
#   uuasm/asm_errors/*.ua    both fail with the diagnostic in <name>.err
#   uuasm/run_errors/*.ua    both fail at run time; russia reports <name>.err
#   uuasm/bootstrap_only/*.ua  russia rejects with <name>.err
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
RUSSIA=src/russia/russia.su
DIR=tests/russia/uuasm
TMP=${TMPDIR:-/tmp}/ussr_uuasm_$$
# A hung run counts as a failure instead of stalling the suite.
LIMIT=
command -v timeout >/dev/null 2>&1 && LIMIT="timeout 120"
mkdir -p "$TMP"
pass=0
fail=0

ok() { pass=$((pass + 1)); printf 'PASS %s\n' "$1"; }
bad() { fail=$((fail + 1)); printf 'FAIL %s: %s\n' "$1" "$2"; }

# run NAME OUTFILE CMD... : stdout+stderr without CR into OUTFILE, echo status
run() {
    out=$1
    shift
    $LIMIT "$@" >"$out.raw" 2>&1
    status=$?
    tr -d '\r' <"$out.raw" >"$out"
    return $status
}

for ua in "$DIR"/*.ua; do
    name=$(basename "$ua" .ua)
    expect="$DIR/$name.expect"
    if run "$TMP/c" "$USSR" "$ua" && cmp -s "$TMP/c" "$expect"; then
        ok "C VM $name"
    else
        bad "C VM $name" "output differs from $expect"
    fi
    if run "$TMP/r" "$USSR" "$RUSSIA" "$ua" && cmp -s "$TMP/r" "$expect"; then
        ok "russia $name"
    else
        bad "russia $name" "output differs from $expect"
    fi
    if run "$TMP/dump.ua" "$USSR" "$RUSSIA" "$ua" dump &&
        run "$TMP/d" "$USSR" "$TMP/dump.ua" && cmp -s "$TMP/d" "$expect"; then
        ok "dump round trip $name"
    else
        bad "dump round trip $name" "disassembly does not reproduce the output"
    fi
done

for ua in "$DIR"/asm_errors/*.ua; do
    name=$(basename "$ua" .ua)
    want=$(cat "$DIR/asm_errors/$name.err")
    if run "$TMP/c" "$USSR" "$ua"; then
        bad "C VM rejects $name" "assembled successfully"
    elif grep -qF "$want" "$TMP/c"; then
        ok "C VM rejects $name"
    else
        bad "C VM rejects $name" "missing '$want'"
    fi
    if run "$TMP/r" "$USSR" "$RUSSIA" "$ua"; then
        bad "russia rejects $name" "assembled successfully"
    elif grep -qF "$want" "$TMP/r"; then
        ok "russia rejects $name"
    else
        bad "russia rejects $name" "missing '$want'"
    fi
done

for ua in "$DIR"/run_errors/*.ua; do
    name=$(basename "$ua" .ua)
    want=$(cat "$DIR/run_errors/$name.err")
    if run "$TMP/c" "$USSR" "$ua"; then
        bad "C VM fails $name" "ran to completion"
    else
        ok "C VM fails $name"
    fi
    if run "$TMP/r" "$USSR" "$RUSSIA" "$ua"; then
        bad "russia fails $name" "ran to completion"
    elif grep -qF "$want" "$TMP/r"; then
        ok "russia fails $name"
    else
        bad "russia fails $name" "missing '$want'"
    fi
done

for ua in "$DIR"/bootstrap_only/*.ua; do
    name=$(basename "$ua" .ua)
    want=$(cat "$DIR/bootstrap_only/$name.err")
    if run "$TMP/r" "$USSR" "$RUSSIA" "$ua"; then
        bad "russia rejects $name" "ran to completion"
    elif grep -qF "$want" "$TMP/r"; then
        ok "russia rejects $name"
    else
        bad "russia rejects $name" "missing '$want'"
    fi
done

rm -rf "$TMP"
printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
