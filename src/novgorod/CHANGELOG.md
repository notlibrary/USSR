# Changelog

## 0.5.0
Refine pass 1 from REFINE.md; the new code is `extras.su`. Nothing
changes for a forum started as before: every addition is a new page or
an option.
- Moderation: `--admin-hash HEX` (the SHA-256 of a password, or
  `NOVGOROD_ADMIN_HASH`) turns on `/mod`: a login that sets a signed,
  HttpOnly cookie for `/mod` only, the report queue, and actions to
  delete a post or thread, lock and sticky threads, resolve reports and
  ban a post's image. Actions go to an append-only `DATA/mod.log`,
  replayed at start, so files, `--db` and `--kvdb` all keep them; `/mod/log`
  is the public log (no reports, no passwords).
- Posters: a "report" link on every post (`/report?no=N`), a password
  field to delete one's own post later (`/delete`, stored hashed in
  mod.log), and sage for replies that should not bump.
- Policy: `DATA/filters.txt` with `word` and `link` rules, refused posts
  with too many lines or one character repeated 80 times, the same text
  again among the last 20 posts, `--read-only` with a banner,
  `--lock-days N` for old threads, banned images.
- Reading: a JSON API (`/api/boards`, `/api/BOARD/threads`,
  `/api/thread/N`) with ETag and 304, Atom feeds per board and thread,
  `/search?q=` across boards, a thread gallery, `/rules` (from
  `DATA/rules.txt`), a per-board notice (`DATA/notice-BOARD.txt`), an
  external-link warning page (`/go?to=`) and `/metrics`.
- Sticky threads come first on boards and in the API; deleted replies
  disappear from pages.
- `--export DIR` writes every post, boards.txt, seq and mod.log, then
  exits: a backup whatever the storage.
- `tests/novgorod_extras_test.su`: 58 checks; the existing
  `tests/novgorod_test.su` output is unchanged.

## 0.4.0
- Thumbnails are made by the server with Omsk (`src/omsk`): every posted
  picture is decoded, its real size replaces the one the form gives, and
  a 200x200 JPEG goes to `thumbs/N.svg`. A picture Omsk cannot read is
  refused; the browser no longer sends a thumbnail. Old boards keep their
  `img/Nt.svg`, which is still served.
- The footer names the ROUND stack (Ryazan, Omsk, Ukraine, Novgorod,
  Dubna) and links to the USSR repository on GitHub.

## 0.3.0
- `--prefix /board` serves the board under a path behind a reverse proxy.
- `deploy/`: install.sh, an Apache snippet and a systemd unit for a board
  with PostgreSQL storage behind an existing Apache.

## 0.2.0
- `--db CONNINFO` keeps posts in PostgreSQL through Dubna; images and
  boards.txt stay in the data folder.

## 0.1.0
- First version: an anonymous image forum for programmers over HTTPS
  (Ryazan TLS, Ukraine HTTP). Boards from `boards.txt`, threads with bump
  order, a bump limit and pruning, replies, pages, a catalog, a home page
  with the latest posts.
- Novgorod markup: bold, italic, strike, spoilers, inline code, fenced code
  blocks with a language label, quotes, `>>N` post links with backlinks and
  hover previews, `>>>/b/` board links, automatic links; everything else
  escaped.
- Images re-encoded in the browser (EXIF removed, at most 2048 pixels, PNG
  or JPEG, 200-pixel thumbnails), stored and served as SVG-wrapped base64
  because USSR strings cannot hold zero bytes.
- Plain-file storage with collision-free numbering by rename; strict CSP,
  same-origin posting, a honeypot field and a forum-wide rate limit.
