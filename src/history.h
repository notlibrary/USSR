#ifndef USSR_HISTORY_H
#define USSR_HISTORY_H

/*
 * The REPL's history file, shared by ussr (bestline, worstline) and the
 * russia.su REPL: one entry per line, oldest first, UTF-8.
 *
 *   USSR_HISTFILE   the file; default .ussr_history in the home folder
 *                   (HOME, else USERPROFILE on Windows); set but empty
 *                   keeps no history file
 *   USSR_HISTSIZE   how many entries to keep, default 1000
 *
 * Only an interactive REPL (stdin a terminal) reads and writes it, so
 * piped sessions and test runs leave a user's history alone.
 */

/* path of name in the home folder, malloc'd, or NULL without one */
char *ussr_home_file(const char *name);

/* Load the history: calls add with each entry, oldest first, and trims
 * the file to USSR_HISTSIZE entries. Remembers the file for
 * ussr_history_append. Returns how many entries were loaded. */
int ussr_history_load(void (*add)(const char *line));

/* Append one entry to the file (after ussr_history_load); empty lines,
 * multi-line text and a repeat of the previous entry are skipped. */
void ussr_history_append(const char *line);

#endif /* USSR_HISTORY_H */
