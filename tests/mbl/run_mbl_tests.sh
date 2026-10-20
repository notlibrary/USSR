#!/bin/sh
# Scripted Mbl editor tests: keystrokes are piped into the editor, which
# reads redirected stdin byte by byte, and the saved file is compared.
# Run from the repository root:  sh tests/mbl/run_mbl_tests.sh [ussr]
USSR=${1:-./ussr}
[ -x "$USSR" ] || USSR=./ussr.exe
MBL=src/Mbl/Mbl.su
TMP=${TMPDIR:-/tmp}/ussr_mbl_$$
mkdir -p "$TMP"
# Under MSYS, programs started through `timeout` get no path translation:
# hand the native Windows path to ussr.
command -v cygpath >/dev/null 2>&1 && TMP=$(cygpath -m "$TMP")
LIMIT=
command -v timeout >/dev/null 2>&1 && LIMIT="timeout 60"
pass=0
fail=0
ESC=$(printf '\033')
BS=$(printf '\177')

# case NAME INITIAL_CONTENT KEYS EXPECTED_CONTENT
# Contents are printf formats; "-" as INITIAL means "no file yet".
case_() {
    name=$1
    file="$TMP/$name.txt"
    rm -f "$file"
    [ "$2" = "-" ] || printf "$2" >"$file"
    printf '%s
' "$3" | $LIMIT "$USSR" "$MBL" "$file" >"$TMP/screen" 2>&1
    printf "$4" >"$TMP/want"
    if cmp -s "$file" "$TMP/want"; then
        pass=$((pass + 1)); printf 'PASS %s\n' "$name"
    else
        fail=$((fail + 1)); printf 'FAIL %s\n' "$name"
        od -c "$file" | head -5
    fi
}

case_ insert_russian - "iПривет, мир!$ESC:wq" 'Привет, мир!\n'
case_ delete_cyrillic_char 'Привет мир\n' "lx:wq" 'Пивет мир\n'
case_ move_over_cyrillic 'абвгд\n' "llx:wq" 'абгд\n'
case_ mixed_latin_cyrillic 'abвгde\n' "lllx:wq" 'abвde\n'
case_ normal_mode_stops_at_last_char 'мир\n' "lllllllx:wq" 'ми\n'
case_ append_after_cyrillic 'мир\n' "llaы!$BS$ESC:wq" 'миры\n'
case_ backspace_multibyte - "iёлка$BS$BS$ESC:wq" 'ёл\n'
case_ backspace_at_line_start_joins 'один\nдва\n' "jiX$BS$BS$ESC:wq" 'одиндва\n'
case_ column_kept_between_lines 'абвгд\nabcde\n' "lljx:wq" 'абвгд\nabde\n'
case_ column_kept_cyrillic_below 'abcde\nабвгд\n' "lljx:wq" 'abcde\nабгд\n'
case_ arrow_keys 'жук\nлес\n' "${ESC}[B${ESC}[Cx:wq" 'жук\nлс\n'
case_ punctuation "«Ёлки», — сказал он.\n" "x:wq" "Ёлки», — сказал он.\n"
case_ enter_splits_cyrillic 'приветмир\n' "llllllixx$BS$BS
$ESC:wq" 'привет\nмир\n'
case_ four_byte_character - "i😀x$ESC:wq" '😀x\n'
case_ delete_four_byte_character '😀a\n' "x:wq" 'a\n'
case_ empty_line_kept 'а\n\nб\n' "jx:wq" 'а\n\nб\n'
case_ byte_order_mark_kept '\357\273\277ёж\n' "x:wq" '\357\273\277ж\n'
case_ cyrillic_file_name_command 'ok\n' ":w $TMP/имя.txt
:q" 'ok\n'
if [ -f "$TMP/имя.txt" ] && cmp -s "$TMP/имя.txt" "$TMP/cyrillic_file_name_command.txt"; then
    pass=$((pass + 1)); printf 'PASS write to a Cyrillic file name\n'
else
    fail=$((fail + 1)); printf 'FAIL write to a Cyrillic file name\n'
fi

# The cursor column counts characters: after "ll" on a Cyrillic line the
# final cursor escape is ESC[1;3H, not the byte column ESC[1;5H.
printf 'щука\n' >"$TMP/col.txt"
printf 'll:q!\r' | $LIMIT "$USSR" "$MBL" "$TMP/col.txt" >"$TMP/screen" 2>&1
if grep -q "$(printf '\033\\[1;3H')" "$TMP/screen" &&
    ! grep -q "$(printf '\033\\[1;5H')" "$TMP/screen"; then
    pass=$((pass + 1)); printf 'PASS cursor column counts characters\n'
else
    fail=$((fail + 1)); printf 'FAIL cursor column counts characters\n'
fi

# A 100-character Cyrillic line is cut at 80 characters (the default width
# without a terminal) and never inside a UTF-8 sequence. "ж" is the two
# bytes 0xD0 0xB6, so the longest run on screen must be exactly 160 bytes
# and must not be followed by a lone 0xD0.
long=$(printf 'ж%.0s' $(seq 1 100))
printf '%s
' "$long" >"$TMP/long.txt"
printf ':q' | $LIMIT "$USSR" "$MBL" "$TMP/long.txt" >"$TMP/screen" 2>&1
run=$(od -An -v -tu1 "$TMP/screen" | tr -s ' 
' '

' | awk '
    $0 == "" { next }
    { b[n++] = $0 }
    END {
        best = 0
        for (i = 0; i < n; ) {
            j = i
            while (j + 1 < n && b[j] == 208 && b[j + 1] == 182) j += 2
            if (j - i > best) { best = j - i; after = (j < n) ? b[j] : -1 }
            i = (j > i) ? j : i + 1
        }
        print best, after
    }')
if [ "$run" = "160 27" ]; then
    pass=$((pass + 1)); printf 'PASS long Cyrillic line cut at 80 characters
'
else
    fail=$((fail + 1)); printf 'FAIL long Cyrillic line cut at 80 characters (%s)
' "$run"
fi

rm -rf "$TMP"
printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
