#ifndef USSR_ALIAS_H
#define USSR_ALIAS_H

#include <stddef.h>

#include "ussr.h"

/*
 * Command aliases: alias(_): "ll" "ls" "-l"
 *
 * After that, ll(_): "/tmp" runs ls(_): "-l" "/tmp": the alias's name is
 * replaced by its first word and the rest are put in front of the call's
 * own arguments. The target may be a program, a builtin or a function.
 *
 * As in an interactive shell, aliases apply to lines typed at the REPL
 * only (each entry is expanded before it is compiled); scripts, libraries
 * and whatever an entry loads or evals never see them, so a personal
 * alias cannot change how a program behaves. kiev.su is the place for
 * them. Not expanded: commands with an explicit path (./ll), commands
 * that define a function or take a block, and a name already expanded on
 * the way (alias(_): "ls" "ls" "-F").
 */

/* Set name to words[0..count); count 0 removes the alias. Returns 0, or
 * -1 with a message in error. */
int ussr_alias_set(const char *name, const char *const *words, size_t count,
                   char *error, size_t error_size);

/* The words of an alias, or NULL; count receives how many. */
const char *const *ussr_alias_find(const char *name, size_t *count);

/* Aliases in the order they were made; NULL past the end. */
const char *ussr_alias_at(size_t index, const char *const **words,
                          size_t *count);

/* Rewrite every aliased command in list, blocks included. */
int ussr_alias_expand(ussr_command_list_t *list);

#endif /* USSR_ALIAS_H */
