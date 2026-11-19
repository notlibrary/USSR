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
printf ':q
' | $LIMIT "$USSR" "$MBL" "$TMP/long.txt" >"$TMP/screen" 2>&1
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

# Refine pass 1: motions, operators, text objects, registers, repeat,
# macros, search, ex commands, settings, insert-mode keys, files
NL=$(printf '\nX'); NL=${NL%X}
CA=$(printf '\001')
CX=$(printf '\030')
CW=$(printf '\027')
CU=$(printf '\025')
TAB=$(printf '\t')
case_ motion_b 'one two three\n' "wwbx:wq" 'one wo three\n'
case_ motion_e 'one two\n' "ex:wq" 'on two\n'
case_ motion_dollar 'abc\n' "\$x:wq" 'ab\n'
case_ motion_caret '  abc\n' "\$^x:wq" '  bc\n'
case_ motion_zero '  abc\n' "\$0x:wq" ' abc\n'
case_ motion_G 'a\nb\nc\n' "Gx:wq" 'a\nb\n\n'
case_ motion_gg 'a\nb\nc\n' "Gggx:wq" '\nb\nc\n'
case_ motion_NG 'a\nb\nc\n' "2Gx:wq" 'a\n\nc\n'
case_ ex_line_number 'a\nb\nc\n' ":3${NL}x:wq" 'a\nb\n\n'
case_ motion_f_repeat 'a,b,c,d\n' "f,;x:wq" 'a,bc,d\n'
case_ motion_t 'a,b,c,d\n' "lt,x:wq" 'a,,c,d\n'
case_ delete_till 'abc,d\n' "dt,:wq" ',d\n'
case_ delete_find 'abc,d\n' "df,:wq" 'd\n'
case_ motion_percent 'f(a(b)c)\n' "%x:wq" 'f(a(b)c\n'
case_ motion_paragraph 'a\nb\n\nc\n' "}x:wq" 'a\nb\n\nc\n'
case_ delete_word 'one two\n' "dw:wq" 'two\n'
case_ delete_to_end 'abc def\n' "wd\$:wq" 'abc \n'
case_ D_deletes_to_end 'abc def\n' "wD:wq" 'abc \n'
case_ delete_to_start 'abc def\n' "wd0:wq" 'def\n'
case_ delete_line_down 'a\nb\nc\n' "dj:wq" 'c\n'
case_ delete_to_last_line 'a\nb\nc\n' "jdG:wq" 'a\n'
case_ undo_operator 'one two\n' "dwu:wq" 'one two\n'
case_ yank_put_line 'a\nb\n' "yyp:wq" 'a\na\nb\n'
case_ yank_word_put_before 'ab cd\n' "ywP:wq" 'ab ab cd\n'
case_ swap_chars 'ab\n' "xp:wq" 'ba\n'
case_ named_register 'one\ntwo\n' "\"ayyj\"ap:wq" 'one\ntwo\none\n'
case_ change_inner_quotes 'say "hi there" ok\n' "fhci\"X$ESC:wq" 'say "X" ok\n'
case_ delete_inner_parens 'call(a, b)\n' "f,di(:wq" 'call()\n'
case_ delete_around_parens 'x(a)y\n' "fada(:wq" 'xy\n'
case_ delete_inner_word 'one two\n' "wdiw:wq" 'one \n'
case_ delete_a_word 'one two three\n' "wdaw:wq" 'one three\n'
case_ replace_char 'abc\n' "rx:wq" 'xbc\n'
case_ toggle_case 'abc\n' "~~:wq" 'ABc\n'
case_ join_lines 'a\n  b\n' "J:wq" 'a b\n'
case_ indent_line 'a\n' ">>:wq" '    a\n'
case_ dedent_line '      a\n' "<<:wq" '  a\n'
case_ upper_word 'hello world\n' "gUiw:wq" 'HELLO world\n'
case_ lower_line 'ABC Def\n' "guu:wq" 'abc def\n'
case_ substitute_char 'abc\n' "sY$ESC:wq" 'Ybc\n'
case_ change_to_end 'abc def\n' "wCX$ESC:wq" 'abc X\n'
case_ open_above 'b\n' "Oa$ESC:wq" 'a\nb\n'
case_ append_line_end 'ab\n' "A!$ESC:wq" 'ab!\n'
case_ insert_line_start '  ab\n' "\$I-$ESC:wq" '  -ab\n'
case_ dot_repeats_x 'abcdef\n' "x..:wq" 'def\n'
case_ dot_repeats_insert 'one\n' "A!$ESC.:wq" 'one!!\n'
case_ dot_repeats_dd 'a\nb\nc\nd\n' "dd.:wq" 'c\nd\n'
case_ macro 'a\nb\nc\n' "qaI-${ESC}jq2@a:wq" '\055a\n\055b\n\055c\n'
case_ search_forward 'foo bar foo\n' "/foo${NL}x:wq" 'foo bar oo\n'
case_ search_wraps 'foo bar foo\n' "/foo${NL}nx:wq" 'oo bar foo\n'
case_ search_backward 'ab cd ab\n' "\$?ab${NL}x:wq" 'ab cd b\n'
case_ search_word 'ab cd ab\n' "*x:wq" 'ab cd b\n'
case_ substitute_first 'a a\nb a\n' ":s/a/X/${NL}:wq" 'X a\nb a\n'
case_ substitute_all 'a a\nb a\n' ":%s/a/X/g${NL}:wq" 'X X\nb X\n'
case_ substitute_groups 'key=value\n' ":s/(\\w+)=(\\w+)/\\2=\\1/${NL}:wq" 'value=key\n'
case_ global_delete 'a1\nb\na2\n' ":g/a/d${NL}:wq" 'b\n'
case_ inverse_global_delete 'a1\nb\na2\n' ":v/a/d${NL}:wq" 'a1\na2\n'
case_ sort_lines 'c\na\nb\n' ":sort${NL}:wq" 'a\nb\nc\n'
case_ sort_numeric '10\n9\n100\n' ":sort n${NL}:wq" '9\n10\n100\n'
case_ sort_unique 'b\na\nb\n' ":sort u${NL}:wq" 'a\nb\n'
case_ range_delete 'a\nb\nc\n' ":1,2d${NL}:wq" 'c\n'
case_ range_to_end 'a\nb\nc\n' ":2,\$d${NL}:wq" 'a\n'
case_ range_shift 'a\nb\n' ":%>${NL}:wq" '    a\n    b\n'
case_ add_number 'x 41 y\n' "${CA}:wq" 'x 42 y\n'
case_ subtract_number 'x 41 y\n' "5${CX}:wq" 'x 36 y\n'
case_ add_negative 'n=-3\n' "${CA}${CA}${CA}${CA}:wq" 'n=1\n'
case_ insert_ctrl_w - "ione two${CW}$ESC:wq" 'one \n'
case_ insert_ctrl_u - "iabc${CU}def$ESC:wq" 'def\n'
case_ insert_tab - "i${TAB}x$ESC:wq" '\tx\n'
case_ expand_tab - ":set et ts=4${NL}i${TAB}x$ESC:wq" '    x\n'
case_ auto_indent '  a\n' ":set ai${NL}A${NL}x$ESC:wq" '  a\n  x\n'
case_ crlf_kept 'a\r\nb\r\n' "x:wq" '\r\nb\r\n'
case_ crlf_to_unix 'a\r\n' ":set ff=unix${NL}:wq" 'a\n'
case_ mark_line_delete 'a\nb\nc\nd\n' "majjd'a:wq" 'd\n'
case_ write_if_changed 'ab\n' "x:x${NL}" 'b\n'
case_ read_only_refuses 'ab\n' ":set ro${NL}x:wq${NL}:q!" 'ab\n'

# +N opens the file at line N
printf 'a\nb\nc\n' >"$TMP/plus.txt"
printf 'x:wq\n' | $LIMIT "$USSR" "$MBL" +3 "$TMP/plus.txt" >"$TMP/screen" 2>&1
printf 'a\nb\n\n' >"$TMP/want"
if cmp -s "$TMP/plus.txt" "$TMP/want"; then
    pass=$((pass + 1)); printf 'PASS +N opens at a line\n'
else
    fail=$((fail + 1)); printf 'FAIL +N opens at a line\n'
fi
# :set nu shows a gutter, the status line a ruler
printf 'a\nb\n' >"$TMP/nu.txt"
printf ':set nu\nj:q\n' | $LIMIT "$USSR" "$MBL" "$TMP/nu.txt" >"$TMP/screen" 2>&1
if grep -q '  2 b' "$TMP/screen" && grep -q '2:1 100%' "$TMP/screen"; then
    pass=$((pass + 1)); printf 'PASS line numbers and ruler\n'
else
    fail=$((fail + 1)); printf 'FAIL line numbers and ruler\n'
fi

# A big file: 50,000 lines (3.5 MB). Edits at the top and in the middle,
# a line deleted and one added, then :wq, within the time limit. Saving
# used to join the text one line at a time (quadratic) and every edit
# copied the whole list, so this took minutes.
awk 'BEGIN { for (i = 1; i <= 50000; i++) printf "line %d: привет, мир — the quick brown fox\n", i }' >"$TMP/big.txt"
awk 'NR == 1 { sub(/^li/, "") } NR == 25000 { print "added"; next } { print }' "$TMP/big.txt" >"$TMP/big_want.txt"
printf 'xx:25000\nddOadded\033oX\033dd:wq\n' | $LIMIT "$USSR" "$MBL" "$TMP/big.txt" >"$TMP/screen" 2>&1
if cmp -s "$TMP/big.txt" "$TMP/big_want.txt"; then
    pass=$((pass + 1)); printf 'PASS a 50000-line file edits and saves\n'
else
    fail=$((fail + 1)); printf 'FAIL a 50000-line file edits and saves\n'
    diff "$TMP/big.txt" "$TMP/big_want.txt" | head -5
fi

rm -rf "$TMP"
printf '%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
