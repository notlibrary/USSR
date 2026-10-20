#!/bin/sh
# Vec2/Vec3 tests. Run from the repository root:
#   sh tests/vectors/run_vector_tests.sh [ussr]
# vectors_test.su must print no FAIL line; every errors/*.su must stop with
# a non-zero status and print the message in its .err file.
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
pass=0
fail=0

out=$("$USSR" tests/vectors/vectors_test.su 2>&1)
status=$?
count=$(printf '%s\n' "$out" | grep -c '^PASS')
if [ $status -eq 0 ] && ! printf '%s\n' "$out" | grep -q '^FAIL'; then
    pass=$((pass + count))
    printf 'PASS vectors_test.su (%s checks)\n' "$count"
else
    fail=$((fail + 1))
    printf '%s\n' "$out" | grep -v '^PASS'
fi

for su in tests/vectors/errors/*.su; do
    name=$(basename "$su" .su)
    want=$(cat "tests/vectors/errors/$name.err")
    out=$("$USSR" "$su" 2>&1)
    status=$?
    if [ $status -ne 0 ] && printf '%s\n' "$out" | grep -qF "$want" &&
        ! printf '%s\n' "$out" | grep -q 'not reached'; then
        pass=$((pass + 1))
        printf 'PASS error %s\n' "$name"
    else
        fail=$((fail + 1))
        printf 'FAIL error %s: %s\n' "$name" "$out"
    fi
done

printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
