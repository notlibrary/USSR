# Novgorod API

`$include "src/novgorod/novgorod.su"` gives the whole forum without starting
a server (its `init` is under `__MAIN__`), which is how
`tests/novgorod_test.su` drives it.

## Markup (`markup.su`)

| Call | Result |
|------|--------|
| `nvm_render(h): text` | a post body as HTML |
| `nvm_inline(h): line 0` | one line of inline markup |
| `nvm_escape(h): text` | `& < > " '` as entities |
| `nvm_clean(t): text` | CR and control bytes dropped, TAB as four spaces, trimmed |
| `nvm_refs(v): text` | numbers quoted with `>>N`, in order |
| `nvm_find(i): s needle from` | first index at or after `from`, or `-1` |
| `nvm_lines(v): text` | lines split at LF |

## Forum (`novgorod.su`)

| Call | Result |
|------|--------|
| `nv_types(_):` | declares `NvPost`, `NvThread`, `NvBoard`, `NvForm`; once per program |
| `nv_setup(_):` | default limits and an empty forum |
| `nv_open(n): dir` | creates or reads a data folder; `n` posts loaded |
| `nv_handle(response): method target request` | a full HTTP response for a raw request |
| `nv_parse_form(form): request` | a multipart request as `NvForm`, or `null` |
| `nv_submit(why): form out` | checks and stores a post; `""` and the post pushed onto `out`, or the reason it was refused |
| `nv_find_post(p): no` | the post with that number, or `null` |
| `nv_threads(v): board` | a board's live threads, latest bump first |
| `nv_load_static(ok): dir` | reads `novgorod.css` and `novgorod.js` |

Limits are globals that `nv_setup` sets and you may change before
`nv_open`: `nv_max_threads` (100), `nv_bump_limit` (300), `nv_page_size`
(10 threads per page), `nv_preview` (3 replies per thread on a board page),
`nv_rate` (30 posts a minute, forum-wide), `nv_max_text` (8000 bytes),
`nv_max_subject` (100), `nv_max_image` and `nv_max_thumb` (base64
characters), `uk_max_body` (2600000 bytes per request), `nv_title`,
`nv_db` (a Dubna conninfo; `""` keeps posts in files) and `nv_db_table`
(`novgorod_posts`).

## Routes

| Route | |
|-------|-|
| `GET /` | boards, rules, latest posts |
| `GET /b/` and `/b/?page=N` | a board: new-thread form, threads with their last replies |
| `GET /b/catalog` | every thread as a tile |
| `GET /b/res/N` | thread N with backlinks and the reply form |
| `GET /p/N` | `303` to post N in its thread |
| `GET /i/N.svg`, `/t/N.svg` | image and thumbnail of post N |
| `GET /markup` | the markup reference with live examples |
| `GET /static/novgorod.css`, `/static/novgorod.js` | assets |
| `POST /post` | `multipart/form-data` with `board`, `thread` (`0` for a new thread), `subject`, `text`, `website` (must stay empty), and for an image `mime` (`jpeg`/`png`), `imgw`, `imgh`, `thw`, `thh`, `thumb`, `img` (base64, last); answers `303` to the new post |

`HEAD` works wherever `GET` does.

## Ukraine additions

Novgorod needed these in `src/ukraine/ukraine.su`:

| Call | Result |
|------|--------|
| `uk_request_header(v): request name` | header value, name case-insensitive, `""` when absent |
| `uk_request_body(b): request` | the bytes after the head |
| `uk_response_ex(r): status type headers body` | `uk_response` with extra `"\n"`-separated header lines |

Requests are read through their `Content-Length` (at most `uk_max_body`,
default 1 MiB; more gets `413`), and responses carry `Content-Length`.
