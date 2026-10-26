#!/bin/bash
# Times each bench/*.su script with ussr and, for the *_small variants,
# through the self-hosted russia.su. Best of three runs, in seconds.
#
#     bash bench/run.sh [ussr]
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
RUSSIA=src/russia/russia.su
TIMEFORMAT=%R

best() {
    b=
    for _ in 1 2 3; do
        t=$( { time "$@" >/dev/null 2>&1; } 2>&1 )
        if [ -z "$b" ] || awk "BEGIN{exit !($t < $b)}"; then b=$t; fi
    done
    echo "$b"
}

for f in bench/*.su; do
    case "$f" in
        *_small.su) printf '%-28s russia %s\n' "$f" "$(best "$USSR" -- "$RUSSIA" "$f")" ;;
        *) printf '%-28s ussr   %s\n' "$f" "$(best "$USSR" "$f")" ;;
    esac
done
