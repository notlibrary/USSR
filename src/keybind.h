#ifndef USSR_KEYBIND_H
#define USSR_KEYBIND_H

#include <stddef.h>

/*
 * REPL key bindings: keybind(_): "Mod+F1" "help(_):"
 *
 * A binding maps one key chord to a line of USSR. When the chord is
 * pressed at the REPL prompt, the line editor replaces whatever was
 * typed with the bound line and submits it, as if it had been typed
 * and Enter pressed.
 *
 * A chord is modifiers joined to a key with '+' (or '-'), any case:
 *   modifiers  Ctrl (Control, C), Alt (Meta, Mod, M), Shift (S)
 *   keys       a..z, 0..9, F1..F12
 * Terminals do not report the Super/Windows key, so Mod means Alt.
 */

#define USSR_KEY_SHIFT 1
#define USSR_KEY_ALT   2
#define USSR_KEY_CTRL  4

/* Keys: lowercase 'a'..'z', '0'..'9', or USSR_KEY_F(1..12). */
#define USSR_KEY_F(n) (0x100 + (n))

/* Parse a chord. Returns 0 and fills mods/key, or -1 with a message in
 * error. Chords the line editor cannot tell apart from editing keys
 * (Ctrl+H/I/J/M are Backspace/Tab/Enter) are rejected. */
int ussr_keybind_parse(const char *spec, int *mods, int *key,
                       char *error, size_t error_size);

/* Canonical name of a chord, e.g. "Alt+F1", into buffer. */
void ussr_keybind_name(int mods, int key, char *buffer, size_t size);

/* Bind a chord to a line; an empty or NULL line removes the binding.
 * Returns 0, or -1 with a message in error. */
int ussr_keybind_set(const char *spec, const char *line,
                     char *error, size_t error_size);

/* Bound line for a chord, or NULL. */
const char *ussr_keybind_find(int mods, int key);

/* Bindings in the order they were made; NULL past the end. */
const char *ussr_keybind_at(size_t index, int *mods, int *key);

/* Decode the bytes one key press sends to a terminal (xterm, VT220,
 * rxvt and Linux console forms) and return the bound line, or NULL. */
const char *ussr_keybind_match_sequence(const char *seq, size_t length);

/* Decode one Windows console _getch() key: prefix is -1 for a plain
 * character (code is that character), or 0x00/0xE0 for an extended
 * key (code is the byte after it). Returns the bound line, or NULL. */
const char *ussr_keybind_match_getch(int prefix, int code);

/* The bytes an xterm sends for the same extended _getch() key (F1..F12
 * with modifiers, Alt+letter, Alt+digit), so Windows console input reads
 * like a terminal's. Returns the length written to out, or 0 for a key
 * with no such form. */
int ussr_keybind_getch_sequence(int prefix, int code,
                                char *out, size_t size);

#endif /* USSR_KEYBIND_H */
