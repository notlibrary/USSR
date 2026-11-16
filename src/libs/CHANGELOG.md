# Library (src/libs) changelog

## 0.2.0

Refine pass 1 from REFINE.md. Every new function is pure USSR; the tests
`tests/lib_text.su`, `tests/lib_collections.su`, `tests/lib_paths.su` and
`tests/lib_formats.su` (226 checks) pass under ussr and under russia.su.

### strings.su
- `str_upper`, `str_title`, and Cyrillic-aware `str_upper_ru` /
  `str_lower_ru` (А-Я, а-я, Ё/ё in UTF-8).
- `str_split`, `str_split_max`, `str_join` (numbers, booleans and null
  converted).
- `str_replace`, `str_replace_n`, `str_rfind`, `str_count`.
- `str_starts`, `str_ends`, `str_contains`: one shared copy for the
  subprojects that each carry their own.
- `str_pad_left`, `str_pad_right`, `str_center`, counting UTF-8
  characters; `str_reverse` keeps characters whole.
- `str_compare`, `str_compare_fold` and `str_natcmp` (natural order:
  `file2` before `file10`); USSR's `<` does not order strings.
- `str_to_int` / `str_from_int_base` for bases 2..36 with `0x`/`0b`/`0o`
  prefixes; `str_group` (thousands separator) and `str_fixed` (exact
  decimals).
- `str_escape` / `str_unescape` for C, JSON and USSR literals, with
  `\uXXXX` decoding to UTF-8 (`str_utf8_char`).
- `str_format("{0} of {1}", args)` with `{{`/`}}` escapes.
- `str_levenshtein`, `str_fuzzy` (subsequence match for completion),
  `str_wrap` (word wrap to a width), `str_plural_ru` / `str_plural_en`,
  `str_translit` (Cyrillic to Latin).

### math.su
- `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`, `log_base`.
- `round(x, digits)` (half away from zero), `trunc`, `sign`, `clamp`,
  `lerp`, `min2`, `max2`.
- `gcd`, `lcm`, `isprime`, `isqrt`, `modpow`.
- Statistics over vectors: `sum`, `product`, `mean`, `median`,
  `variance`, `stddev`, `percentile`.
- Bit helpers: `popcount`, `rotl32`, `rotr32`, `bswap32`.
- `random_range`, `random_float`.

### New modules
- `lists.su`: range, copy, reverse, slice/take/drop, concat, index_of,
  contains, count, unique, insert/remove_at/remove/set (copies, since
  vectors only grow at the end), stable merge sort (ascending, descending,
  natural), min/max, zip, flatten, chunk, fill, equal; set union,
  intersection and difference; a FIFO queue and a priority queue kept in
  btrees.
- `encoding.su`: Base64 (standard and URL alphabets), hex, URL
  percent-encoding, form encoding and query parsing, FNV-1a, Adler-32,
  hex digests over the `crc32`/`sha256`/`md5` builtins, `uuid4` and
  `uuid_valid`.
- `paths.su`: `path_join`, `path_dir`, `path_base`, `path_ext`,
  `path_stem`, `path_is_abs`, `path_normalize`, `path_abs`, `path_split`;
  `exists`, `read_lines`, `write_lines`, `append_line`, `read_text`,
  `write_text`, `write_file_atomic`; `walk_dir`, `glob` (`*`, `?`, `**`),
  `copy_dir`, a guarded `remove_tree`; `temp_dir`, `temp_path`;
  `env_get`, `env_expand` (`$NAME`, `${NAME}`, `~`), `env_path_list`,
  `which`.
- `dates.su`: leap years, month lengths, civil date <-> day number,
  weekday and yearday, `gmtime` / `timegm`, ISO 8601 formatting and
  parsing (with zone offsets), `strftime`, month and day names,
  `elapsed_ms`.
- `json.su`: `json_parse` (objects as btrees, arrays as vectors, surrogate
  pairs, error offsets), `json_stringify`, `json_pretty`, `json_get` with
  a dotted path.
- `args.su`: a command-line parser with flags, options with values and
  defaults, `--name=value`, `-abc`, `-ovalue`, `--`, errors and generated
  `--help` text.
- `textproc.su`: `html_escape`, `sql_quote`, `shell_quote`,
  `template_render` (`${name}` holes with html/sql/shell escaping), an LCS
  line diff (`diff_lines`, `diff_unified`) and `patch_apply`, and a
  levelled logger to stderr and an optional file.
- `testing.su`: `assert_eq`, `assert_true`, `assert_null`, `assert_near`,
  counters and `test_summary`.

### Notes
- Library functions avoid struct declarations and top-level state, since
  programs with `init` skip top-level commands; queues, heaps, JSON objects
  and parsers use btrees.
- The compiler's "set but never read, while other code reads it" warning
  counts reads of other functions' locals, so a library local named like a
  caller's unread result (`ok`) set it off; the library avoids such names.

## 0.1.0

- `standard.su`, `io.su`, `bytes.su`, `math.su`, `strings.su` and
  `vectors.su`.
