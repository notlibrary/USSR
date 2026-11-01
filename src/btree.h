#ifndef USSR_BTREE_H
#define USSR_BTREE_H

/*
 * The btree container: an ordered map from keys to values kept in a
 * B+ tree. Keys are numbers or strings: numbers sort before strings,
 * integers and reals compare as numbers (1 and 1.0 are one key) and
 * strings compare byte by byte. Values are any USSR value.
 *
 * Like vectors and structs a btree is a reference type: copying the
 * value shares the tree, and btput/btdel change it in place for every
 * holder. The mark-sweep collector (gc.c) finds trees that only
 * cycles keep alive.
 *
 * The USSR commands (ussr_btree_dispatch):
 *   btree(t):                    a new, empty tree
 *   btput(t): t key value        stores value under key; the tree
 *   btget(v): t key [default]    the value, or default (null)
 *   bthas(b): t key              true when key is in the tree
 *   btdel(b): t key              removes key; true when it was there
 *   btlen(n): t                  the number of keys (len(n): t too)
 *   btfirst(k): t / btlast(k): t the smallest / largest key, or null
 *   btfloor(k): t key            the largest key <= key, or null
 *   btceil(k): t key             the smallest key >= key, or null
 *   btprev(k): t key             the largest key < key, or null
 *   btnext(k): t key             the smallest key > key, or null
 *   btrange(v): t [lo [hi [max]]] [key, value] pairs, lo <= key < hi
 *                                in order; null lo or hi is open; at
 *                                most max pairs
 *   btkeys(v): t [lo [hi [max]]] the keys only, the same way
 */

#include "ussr.h"     /* ussr_btree_t, values, arguments */

ussr_btree_t *ussr_btree_create(void);
void ussr_btree_retain(ussr_btree_t *tree);
void ussr_btree_release(ussr_btree_t *tree);
size_t ussr_btree_count(const ussr_btree_t *tree);

/* 1 when key can be a key (a number or a string). */
int ussr_btree_key_ok(const ussr_value_t *key);
/* -1, 0 or 1: the order of two valid keys. */
int ussr_btree_key_compare(const ussr_value_t *a, const ussr_value_t *b);

/* Stores copies of key and value; 0, or -1 for a bad key or no memory. */
int ussr_btree_put(ussr_btree_t *tree, const ussr_value_t *key,
                   const ussr_value_t *value);
/* A pointer to the stored value, or NULL. */
const ussr_value_t *ussr_btree_get(const ussr_btree_t *tree,
                                   const ussr_value_t *key);
/* 1 when key was removed, 0 when it was not there. */
int ussr_btree_delete(ussr_btree_t *tree, const ussr_value_t *key);

/* Calls fn(key, value, ctx) on every pair in key order until it
 * returns nonzero; returns that, or 0. */
int ussr_btree_each(const ussr_btree_t *tree,
                    int (*fn)(const ussr_value_t *key,
                              const ussr_value_t *value, void *ctx),
                    void *ctx);

/* Mark-sweep support (gc.c). */
ussr_btree_t *ussr_btree_gc_list(void);
ussr_btree_t *ussr_btree_gc_next(const ussr_btree_t *tree);
int ussr_btree_gc_marked(const ussr_btree_t *tree);
void ussr_btree_gc_set_mark(ussr_btree_t *tree, int mark);
/* Calls fn on every stored value. */
void ussr_btree_each_value(ussr_btree_t *tree,
                           void (*fn)(ussr_value_t *value));
void ussr_btree_gc_unregister(ussr_btree_t *tree);
/* Drops one reference held by a garbage object, never freeing. */
void ussr_btree_gc_drop_ref(ussr_btree_t *tree);
/* Frees the tree, its keys and its scalar values without releasing
 * the vectors, structs and trees it holds (the sweep frees those). */
void ussr_btree_destroy_raw(ussr_btree_t *tree);

/* The bt* commands: 1 done, -1 failed (with a message), 0 not one of
 * them. */
int ussr_btree_dispatch(const char *command, ussr_argument_t *arguments,
                        size_t argument_count, ussr_value_t *out_result);

#endif
