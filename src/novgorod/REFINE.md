# Novgorod — refinement ideas

**Today:** an anonymous HTTPS image forum (0.4.0) on the ROUND stack.
- Boards come from `boards.txt`. There are threads with bump order, a
  bump limit and pruning, replies, pages, a catalog and a home page.
- Markup has bold, italic, strike, spoiler, code and fences, quotes,
  `>>N` links with backlinks and hover previews, board links and
  autolinks. Everything else is escaped.
- Images are re-encoded in the browser, Omsk makes the thumbnails, and
  files are stored as SVG-wrapped base64.
- Storage is files, PostgreSQL (`--db`) or Dubna KV (`--kvdb`).
- Security: CSP, a same-origin check, a honeypot, a forum-wide rate
  limit, `--prefix`, HSTS, and an Apache/systemd deploy.

## Moderation
- An admin login (one password hash in config, sessions by a signed
  cookie, used for admin pages only)
- Delete post or thread, ban by IP hash with an expiry, sticky and lock
  threads
- Report button, a report queue, and moderator notes
- A word filter and a link blocklist
- Per-IP rate limits as well as the forum-wide one, with tunable
  windows
- Proof-of-work or a simple challenge for posting under flood, with no
  third-party CAPTCHA
- A moderation log page (public or private)

## Posting
- Several images per post, and video or GIF thumbnails (first frame)
- Image hashing to refuse reposts and banned images (perceptual hash)
- Spoiler images
- Deletion by the poster: a password field stored hashed with the post
- Tripcodes (optional, per board), since names are off by default
- Sage (reply without bumping)
- A post preview before submit
- A draft kept in the browser's localStorage
- Post edit window of a few minutes
- Polls in threads

## Markup
- Tables, ordered and unordered lists, headings in long posts
- Syntax highlighting of fenced code by language (server-side, so it
  works without JavaScript)
- Inline math (`$...$`) rendered to MathML
- `>>N` to posts in other threads with cross-thread previews
- Emoji shortcodes, kept text-first

## Reading
- Thread auto-update (polling or Server-Sent Events) with a new-posts
  counter
- "You" markers for one's own posts (kept in localStorage)
- Search across boards (PostgreSQL full-text, or a KV inverted index)
- RSS/Atom feeds per board and per thread
- A JSON API (`/api/b/threads`, `/api/thread/N`) for clients and bots
- Archive of pruned threads (read-only) in place of deleting them
- Gallery view of a thread's images
- Pagination settings and infinite scroll as an option
- Themes beyond dark/light, and per-board accent colours

## Storage and scale
- Store images as raw bytes now that Dubna/bytes builtins exist, without
  the SVG-base64 wrapper (a third smaller)
- Content-addressed image files (hash names) and deduplication
- An object-storage backend (S3-compatible) for images
- Migration tool between files, KV and PostgreSQL storage
- Caching of rendered threads (ETag and `If-None-Match`, 304 responses)
- gzip/brotli responses
- HTTP keep-alive and HTTP/2 once Ukraine supports them
- Board config hot reload, without a restart

## Operations
- Metrics endpoint (posts/min, response times, rate-limit hits)
- Structured access log with IP hashing for privacy
- Backups: `novgorod --export DIR` / `--import`
- Docker image and a Moscow ISO service entry

## Tests
- Browser end-to-end tests (post, reply, preview, hover) driven
  headlessly
- Load tests: concurrent posting with the scheduler
- A markup fuzz test asserting no tag injection
