#ifndef USSR_HELP_H
#define USSR_HELP_H

/*
 * help(_):            overview: syntax, REPL keys, every builtin by group
 * help(_): "name"     usage of one builtin, or a topic (keys, ai, syntax)
 *
 * Prints to stdout. Returns 0, or -1 when name is unknown (after
 * printing the names that start the same way, if any).
 */
int ussr_help_print(const char *topic);

#endif /* USSR_HELP_H */
