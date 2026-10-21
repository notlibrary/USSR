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
CR=$(printf '\022')

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

# Counts and word motions
case_ count_right 'abcdef\n' "3lx:wq" 'abcef\n'
case_ count_with_zero 'abcdefghijklm\n' "10lx:wq" 'abcdefghijlm\n'
case_ count_down 'a\nb\nc\nd\n' "2jx:wq" 'a\nb\n\nd\n'
case_ count_up_cyrillic 'один\nдва\nтри\n' "2jl2kx:wq" 'оин\nдва\nтри\n'
case_ count_delete_chars 'abcdefg\n' "l3x:wq" 'aefg\n'
case_ count_delete_stops_at_line_end 'abc\nxyz\n' "l9x:wq" 'a\nxyz\n'
case_ count_delete_lines 'a\nb\nc\nd\n' "j2dd:wq" 'a\nd\n'
case_ count_arrow_keys 'abcdef\n' "2${ESC}[Cx:wq" 'abdef\n'
case_ word_forward 'one two three\n' "wx:wq" 'one wo three\n'
case_ word_forward_count 'one two three\n' "2wx:wq" 'one two hree\n'
case_ word_forward_punctuation 'foo, bar\n' "wx:wq" 'foo bar\n'
case_ word_forward_next_line 'one\ntwo\n' "wx:wq" 'one\nwo\n'
case_ word_forward_cyrillic 'привет мир\n' "wx:wq" 'привет ир\n'
case_ change_word 'hello world\n' "cwXYZ$ESC:wq" 'XYZ world\n'
case_ change_word_mid_word 'hello world\n' "llcwy$ESC:wq" 'hey world\n'
case_ change_last_word 'one two\n' "wcwX$ESC:wq" 'one X\n'
case_ change_punctuation 'foo, bar\n' "lllcw;$ESC:wq" 'foo; bar\n'
case_ change_blanks 'a   b\n' "lcw_$ESC:wq" 'a_b\n'
case_ change_word_count_before 'one two three\n' "2cwX$ESC:wq" 'X three\n'
case_ change_word_count_after 'one two three\n' "c2wX$ESC:wq" 'X three\n'
case_ change_word_counts_multiply 'a b c d e f g\n' "2c2wX$ESC:wq" 'X e f g\n'
case_ change_word_count_past_end 'one two\n' "5cwX$ESC:wq" 'X\n'
case_ change_word_empty_line 'a\n\nb\n' "jcwnew$ESC:wq" 'a\nnew\nb\n'
case_ change_word_cyrillic 'привет мир\n' "cwздравствуй$ESC:wq" 'здравствуй мир\n'
case_ change_word_mixed 'Мир-world 2\n' "cwX$ESC:wq" 'X-world 2\n'
case_ change_word_guillemet '«слово»\n' "cw<$ESC:wq" '<слово»\n'
case_ change_word_dash 'да — нет\n' "wcw-$ESC:wq" 'да - нет\n'
case_ change_word_last_line_no_newline 'end' "cwX$ESC:wq" 'X\n'
case_ c_then_other_key_cancels 'abc\n' "cxx:wq" 'bc\n'
case_ escape_clears_count 'abcdef\n' "3${ESC}lx:wq" 'acdef\n'

# Undo (u) and redo (Ctrl+R)
case_ undo_delete_char 'abc\n' "xu:wq" 'abc\n'
case_ undo_counted_x_is_one_change 'abcdef\n' "3xu:wq" 'abcdef\n'
case_ undo_insert_session 'ab\n' "iXYZ${ESC}u:wq" 'ab\n'
case_ undo_append_session 'ab\n' "aXY${ESC}u:wq" 'ab\n'
case_ undo_delete_line 'a\nb\nc\n' "jddu:wq" 'a\nb\nc\n'
case_ undo_counted_dd 'a\nb\nc\n' "2ddu:wq" 'a\nb\nc\n'
case_ undo_delete_only_line 'solo\n' "ddu:wq" 'solo\n'
case_ undo_change_word 'hello world\n' "cwX${ESC}u:wq" 'hello world\n'
case_ undo_open_line 'a\n' "onew${ESC}u:wq" 'a\n'
case_ undo_enter_and_join 'один\nдва\n' "jiX$BS$BS
${ESC}u:wq" 'один\nдва\n'
case_ undo_multiple_levels 'abc\n' "xxuu:wq" 'abc\n'
case_ undo_count 'abcd\n' "xxx2u:wq" 'bcd\n'
case_ undo_past_oldest 'abc\n' "xuuuu:wq" 'abc\n'
case_ undo_nothing 'abc\n' "u:wq" 'abc\n'
case_ undo_skips_empty_insert 'abc\n' "xi${ESC}u:wq" 'abc\n'
case_ undo_cyrillic 'привет\n' "xxu:wq" 'ривет\n'
case_ undo_restores_cursor 'abc\ndef\n' "jxkux:wq" 'abc\nef\n'
case_ redo 'abc\n' "xxuu${CR}:wq" 'bc\n'
case_ redo_twice 'abc\n' "xxuu${CR}${CR}:wq" 'c\n'
case_ redo_count 'abcd\n' "xxx3u2${CR}:wq" 'cd\n'
case_ redo_past_newest 'abc\n' "xu${CR}${CR}${CR}:wq" 'bc\n'
case_ new_change_drops_redo 'abc\n' "xulx${CR}:wq" 'ac\n'
case_ empty_insert_keeps_redo 'abc\n' "xui${ESC}${CR}:wq" 'bc\n'
case_ undo_after_write 'abc\n' "x:w
u:wq" 'abc\n'
case_ edit_command_resets_history 'abc\n' "x:e
u:wq" 'abc\n'

# The [+] marker follows the saved text: after "x :w x u" the text equals
# the written file again, after "x :w u" it differs from it.
marker() {
    printf 'abc\n' >"$TMP/mark.txt"
    printf "$1" | $LIMIT "$USSR" "$MBL" "$TMP/mark.txt" 2>&1 |
        tr '\033' '\n' | grep 'mark\.txt' | tail -1 | grep -c '\[+\]'
}
if [ "$(marker 'x:w\nxu:q\n')" = 0 ] && [ "$(marker 'x:w\nu:q\n')" = 1 ] &&
    [ "$(marker 'x:q\n')" = 1 ] && [ "$(marker 'xu:q\n')" = 0 ]; then
    pass=$((pass + 1)); printf 'PASS undo keeps the modified marker right\n'
else
    fail=$((fail + 1)); printf 'FAIL undo keeps the modified marker right\n'
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
