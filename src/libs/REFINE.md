# library (src/libs) — refinement ideas

**Today:** the standard library has six files.
- `standard.su`: `random64`, `seed_random64` and `time`.
- `io.su`: `template`, `scan` and file streams; `list_dir`, `is_dir`,
  `make_dir`, `rename_path`, `copy_file` and the remove calls;
  `putch` / `getch`.
- `bytes.su`: `set_bytes`, `read_bytes`, `write_bytes`, `text_bytes`, and
  `serialize` / `deserialize` (INT only).
- `math.su`: sqrt, trig and inverse trig, ln/exp/pow, floor/ceil, cbrt,
  hypot, log2/log10 and great-circle distance.
- `strings.su`: number formatting and parsing, lower, find, trim, repeat
  and `utf8_*`.
- `vectors.su`: Vec2/Vec3 maths.

## strings.su
- `str_upper`, `str_title`, Unicode-aware case mapping (Cyrillic
  included)
- `str_split(v): s sep [max]`, `str_join(s): v sep`
- `str_replace(s): s from to [count]`, `str_rfind`, `str_count`
- `str_starts`, `str_ends` and `str_contains` (every subproject has its
  own copy: kgb, km_, nv_, sb_)
- `str_pad_left`, `str_pad_right` and `str_center` (also repeated across
  subprojects)
- `str_reverse` that is UTF-8-safe
- `str_format(s): "{0} of {1}" args`, a format with named and positional
  holes
- `str_to_int(r): s base` and `str_from_int_base(r): n base` (hex, octal,
  binary)
- Number formatting with a thousands separator and fixed decimals
- `str_escape` and `str_unescape` for C, JSON and USSR string literals
- Levenshtein distance and fuzzy matching (for shell completion)
- Unicode normalisation (NFC/NFD) and grapheme cluster iteration
- Display width with East Asian Wide characters (Mbl, Mbltishi and Sochi
  need it)
- Regex helpers built on the VM's regex: `re_match`, `re_find_all`,
  `re_replace`, `re_split` (AGENTS.md: advanced regex)

## Collections
- A `lists.su`: `map`, `filter`, `reduce`, `sort` with a comparator
  name, `reverse`, `slice`, `index_of`, `unique`, `zip`, `range`
- Insert and delete in the middle of a vector (Omsk notes vectors only
  grow at the end)
- A set type and set operations, plus a queue/deque and a priority heap
- Wrappers on the btree container: ordered maps with range iteration

## Data formats
- A JSON parser and serialiser, which Lenin and Novgorod need
- CSV read/write with quoting (Siberia has its own; move it here)
- An XML reader (Siberia has one) and an INI / `key=value` reader
  (Kremlin and Lenin have their own)
- Base64, hex and URL percent-encoding (Dubna, Omsk and Novgorod each
  have their own copy)
- `serialize` / `deserialize` for "INT32", "INT16", "FLOAT64", "STR" and
  both endiannesses
- CRC-32, Adler-32 and FNV-1a (Stalin, Omsk and Dubna KV each have one)

## io.su
- Path helpers: `path_join`, `path_dir`, `path_base`, `path_ext`,
  `path_normalize`, `path_abs` (Kremlin, Tver and KGB have their own)
- `walk_dir` recursion, `glob(v): "src/**/*.su"`
- `stat(v): path`: size, modification time and kind; `exists`
- A recursive `copy_dir` (Tver has one) and a guarded `remove_tree`
- `temp_file` and `temp_dir`
- An atomic `write_file_atomic` (write to a temporary file, then rename)
- `read_lines(v): path` and `write_lines(n): path v`
- Environment helpers: `env_get`, `env_set`, `env_list`, `env_expand`
  ("$HOME/x"), in line with the AGENTS.md environment-variable goal

## math.su
- `tanh`, `sinh` and `cosh` with their inverses; `log(b, x)`;
  `round(x, digits)`; `trunc`; `sign`; `clamp`; `lerp`
- `gcd`, `lcm`, `isprime`, `modpow`, integer `isqrt`
- Statistics: mean, median, variance, stddev, percentile
- `random_range(lo, hi)`, `random_float`, `shuffle`, `choice`, plus a
  seeded independent generator object
- Matrices: Mat2/Mat3/Mat4, multiply, inverse, transforms (Tula's CAD
  could use them)
- Vec4 and quaternions
- Big integers for exact arithmetic (Klin, crypto helpers)

## Time
- `localtime`, `gmtime`, `strftime`-style formatting, ISO 8601 parsing
  (Kremlin has date code of its own)
- Monotonic clock and `elapsed_ms` for benchmarks
- Day-of-week and leap-year helpers in one place

## Library quality
- One prefix rule and a namespace convention (`str_`, `io_`, `m_`)
  written down
- A doc generator from the header comments into `docs/`
- Unit tests per function in `tests/lib_*`, also run through russia.su
- A `$once` guard on every file, so mixed includes are safe
- A list of each subproject's local helper copies to move into the
  library

## Text processing
- A template engine for strings (`${name}` holes with escaping modes
  for HTML, SQL literals and shell words)
- Word wrap to a width (`str_wrap`), used by Kremlin labels and Sochi
- Natural-order comparison (`file2` before `file10`) and case-folded
  comparison for sorting
- Transliteration between Cyrillic and Latin
- Plural forms for Russian and English messages (`1 файл`, `2 файла`,
  `5 файлов`)
- A command-line argument parser (`--name value`, `-abc`, `--`,
  generated `--help`), since Lenin, Novgorod, Stalin, Sochi and usa each
  parse flags by hand
- A logging module: levels, timestamps, a file sink and stderr
- A diff of two line vectors (Myers) and a patch applier
- Hash functions as library calls: SHA-1, SHA-256, MD5 wrappers over
  the builtins, and SipHash for tables
- UUID generation and parsing (Stalin has GUID code)
- A hash map with any key type, iteration order kept

## Concurrency and system
- Channels and mutexes on the scheduler (`chan_send`, `chan_recv`,
  `lock`), in place of each app's own lock (`uilock`)
- `async_all` / `async_any` over a vector of tasks, with timeouts
- A process module: run a program with arguments, capture its output
  and exit status, feed its input, set its directory and environment
- Signal and exit hooks (`at_exit`) to restore terminals and delete
  temporary files
- Terminal helpers in one place: raw mode, key decoding, colours,
  cursor, size (Mbl and Sochi each have their own)
- A test helper library (`assert_eq`, `assert_near`, counters, a
  summary line) shared by `tests/`

## More maths
- Decimal fixed-point type for money
- Numeric integration, root finding and interpolation
- 2D geometry: segment and circle intersections, polygon area, point
  in polygon (Tula)
- Bit operations as functions: rotate, popcount, byte swap (Klin has
  them)
