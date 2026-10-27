# Novgorod

Novgorod is an anonymous image forum for programmers, written in USSR and
served over HTTPS. It is the N of the **RUND stack**:

| Layer | Name | Job |
|-------|------|-----|
| R | [Ryazan](../ryazan/) | TLS: the operating system's Schannel or OpenSSL |
| U | [Ukraine](../ukraine/) | HTTP/1.1 server and request-handler framework |
| N | Novgorod | this forum |
| D | [Dubna](../dubna/) | PostgreSQL client (`--db`) |

No accounts, no names, no cookies, no tracking. Boards, threads, replies,
`>>123` quotes with backlinks, a catalog, and code-friendly
[markup](docs/MARKUP.md). And no anime.

## Run it

From the repository root, with a ussr built with TLS (every Windows build;
OpenSSL elsewhere):

```bash
ussr src/novgorod/novgorod.su
```

Then open <https://localhost:8443/>. Without `--cert` Novgorod serves the
test certificate in `tests/ryazan/`, whose private key is public, so your
browser will warn about it. That is fine on your own machine. For anything
else, bring a real certificate:

```bash
ussr src/novgorod/novgorod.su --host 0.0.0.0 --port 443 --cert forum.p12 --secret "password" --hsts
```

| Option | Default | Meaning |
|--------|---------|---------|
| `--host` | `127.0.0.1` | address to listen on |
| `--port` | `8443` | port |
| `--data DIR` | `novgorod-data` | posts, images, `boards.txt` |
| `--static DIR` | found | folder with `novgorod.css` and `novgorod.js` (looks in `src/novgorod/static`, `static`, `DIR/static`) |
| `--cert FILE` `--secret PASS` | test certificate | PKCS#12 certificate and its password (OpenSSL: PEM chain and key file too) |
| `--http` | off | plain HTTP, for a TLS-terminating proxy in front |
| `--prefix /board` | none | serve under a path: a reverse proxy strips it, Novgorod writes it into links and redirects |
| `--hsts` | off | send `Strict-Transport-Security` |
| `--db CONNINFO` | files | keep posts in PostgreSQL through Dubna: `host=... dbname=... user=... password=...` or `postgres://user:pass@host/db?sslmode=require` |

## Behind Apache at /board

[deploy/](deploy/) installs a board with PostgreSQL storage on an Ubuntu
server that already runs Apache: one script, see
[deploy/README.md](deploy/README.md).

## Boards

`DIR/boards.txt` is written on the first run and read on every start, one
board per line:

```
# name | title | description
prog | Programming | General programming: problems, ideas, tools and careers
c | C and systems | C, C++, Rust, assembly, kernels, compilers and linkers
lang | Languages | Language design, type systems, interpreters and virtual machines
web | Web and networks | HTTP, TLS, browsers, servers and protocols
ussr | USSR | The USSR shell, the RUND stack and the rest of the family
meta | Meta | Novgorod itself: bugs, rules and wishes
```

Names are 1 to 16 lowercase letters or digits. Edit the file and restart.

## How images work

USSR strings stop at a zero byte, so raw image bytes cannot travel through
the server. The browser does the binary work instead:

1. When you post, `novgorod.js` draws the picked image onto a canvas, at
   most 2048 pixels on its longest side, and encodes it as PNG (screenshots
   and diagrams) or JPEG. It also draws a 200-pixel JPEG thumbnail.
   Redrawing removes EXIF data (camera, GPS position) and anything hidden
   after the picture.
2. Both go to the server as base64 form fields. The server checks the
   alphabet, the size, the JPEG/PNG signature and the dimensions.
3. Each is stored as an SVG file that wraps the base64 in a `data:` URL.
   Browsers show `/i/N.svg` and `/t/N.svg` like any image, cached forever.

Attaching an image needs JavaScript; text posts work without it. An
animated GIF keeps its first frame.

## Safety

- Every byte of a post that is not markup is HTML-escaped. Links only come
  from `http://` and `https://` URLs and open with
  `rel="nofollow noopener noreferrer ugc"`.
- HTML pages carry a strict Content-Security-Policy (no inline script, no
  third-party anything), `X-Frame-Options: DENY` and
  `Referrer-Policy: same-origin`. Image SVGs get their own CSP that allows
  nothing but `data:` images.
- Posts from other sites' pages are refused (`Sec-Fetch-Site`, `Origin`).
- A hidden form field catches simple bots; a forum-wide limit (30 posts a
  minute by default) slows floods. The socket API does not expose client
  addresses, so there is no per-address limit or ban list yet.
- Requests over 2.6 MB are answered `413` before the body is read.

## Storage

`DIR/posts/N.post` holds one post: a few `key value` lines, a blank line,
the text as typed. `DIR/img/N.svg` and `DIR/img/Nt.svg` hold its image and
thumbnail, `DIR/seq` the next post number. A new number is taken by
renaming a temporary file to `posts/N.post`, which never replaces an
existing file, so two posts can never share a number. Boards keep 100
threads; the thread bumped longest ago is deleted, with its files, when a
new one would make 101. Replies stop bumping a thread after 300.

With `--db`, posts live in the table `novgorod_posts` instead (created on
the first start; the role needs CREATE on its schema, which PostgreSQL 15+
does not grant on `public` by default). The database numbers posts with a
sequence; boards.txt and the images stay in the data folder. Every write
opens its own connection, so request tasks never share one. Posts already
in files are not copied into the database.

```bash
ussr src/novgorod/novgorod.su --db "postgres://forum:secret@localhost/forum?sslmode=require"
```

## Files

| File | |
|------|-|
| `novgorod.su` | the server: storage, forms, pages, routes |
| `markup.su` | Novgorod markup to safe HTML |
| `static/novgorod.js` | image re-encoding, quoting, previews, image expansion |
| `static/novgorod.css` | the look, light and dark |
| `docs/API.md` | functions for embedding and testing |
| `docs/MARKUP.md` | the markup reference |
| `tests/TESTPLAN.md` | what is tested and how |

Tests: `ussr tests/novgorod_test.su` (also part of `tests/run_all.sh`);
`DUBNA_TEST_URL=... ussr tests/novgorod_db_live.su` for `--db`.
