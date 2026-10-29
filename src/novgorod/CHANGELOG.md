# Changelog

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
