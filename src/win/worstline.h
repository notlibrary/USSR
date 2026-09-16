#ifndef USSR_WORSTLINE_H
#define USSR_WORSTLINE_H

/*
 * Windows console line editor -- deliberately NOT named bestline* so
 * grepping for a symbol always tells you unambiguously which
 * implementation (this one, or the real POSIX bestline library)
 * you're looking at, per request. Same three entry points bestline.h
 * provides, same signatures, different names:
 *
 *   worstline           <-> bestline
 *   worstlineFree       <-> bestlineFree
 *   worstlineHistoryAdd <-> bestlineHistoryAdd
 *
 * main.c's actual call sites still just say bestline(...)/etc. -- the
 * #defines below make that resolve to this file's real worstline*
 * functions on Windows, so main.c needs no per-platform code beyond
 * the one #ifdef _WIN32 that picks this header over bestline.h.
 *
 * Supported editing: printable character insert at cursor, Backspace,
 * Delete, Left/Right/Home/End, Up/Down for history recall (with the
 * in-progress line preserved so paging back down returns to it,
 * matching readline's behavior), and Ctrl-D-as-EOF (returns NULL on
 * an empty line, deletes-forward on a non-empty one, same as
 * GNU readline). Not implemented: tab completion, reverse search,
 * multi-line editing, hints -- none of which main.c's REPL loop uses.
 */

char *worstline(const char *prompt);
void worstlineFree(char *line);
void worstlineHistoryAdd(const char *line);

#define bestline worstline
#define bestlineFree worstlineFree
#define bestlineHistoryAdd worstlineHistoryAdd

#endif
