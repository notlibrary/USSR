# Novgorod test plan

Automated:

- `ussr tests/novgorod_test.su` (any build, also run through russia.su by
  `tests/run_all.sh`; no socket needed). Markup: escaping, emphasis and
  nesting, lone stars, code spans and fences, backslash escapes, `>>N` and
  `>>>/b/` links, URLs (and no `javascript:`), quotes, blank lines, cleaning,
  reference lists. Then a scratch forum in `tests/novgorod_tmp`: a thread with
  an image and a reply through real multipart requests; the thread, board,
  catalog, home and markup pages; backlinks; image and thumbnail SVGs; `/p/N`
  redirects; 404s; HEAD; refusals (honeypot, unknown board, empty post,
  missing thread, reply to a reply, image type mismatch, bad base64,
  another site's origin, not multipart); everything read back after a
  restart and numbering carried on; old threads pruned with their files;
  the forum-wide rate limit.

By hand (done for 0.1.0 in the desktop app's browser and with curl):

- `ussr src/novgorod/novgorod.su` and `curl --cacert
  tests/ryazan/localhost.cert.pem https://localhost:8443/` answer over TLS.
- Post a PNG screenshot and a 5 MB 3000×2000 JPEG photo from the board
  page: the photo is shrunk to 2048 pixels and fits; both show as
  thumbnails, expand in place on click and open full size in a new tab.
- Click a post number: `>>N` lands in the reply box (from a board page, on
  the thread page). Hover a `>>N` link: the post shows as a preview.
- A 2 MB image post over HTTPS through curl gets `303`; a 4 MB request gets
  `413 Content Too Large`.
- Without JavaScript, text posts work and the form says images need it.
- Dark and light colour schemes; a 375-pixel-wide window.
