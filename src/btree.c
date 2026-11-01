#include "btree.h"
#include "uno.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ussr_argument_evaluate() is implemented by the runtime (ussr.c). */
extern int ussr_argument_evaluate(
    const ussr_argument_t *argument,
    ussr_value_t *result
);

/*
 * A B+ tree: every key and value lives in a leaf, the leaves are linked
 * in key order, and the internal nodes hold separator keys only.
 *
 * An internal node with n keys has n + 1 children; child i holds the
 * keys k with keys[i-1] <= k < keys[i]. A node holds at most BT_MAX keys
 * and, unless it is the root, at least BT_MIN: an insert that overflows
 * a node splits it, a delete that underflows one borrows from a sibling
 * or merges with it. The arrays have room for one key more than BT_MAX,
 * the moment between an insert and its split.
 */

#define BT_MAX 32
#define BT_MIN (BT_MAX / 2)

typedef struct bt_node
{
    int leaf;
    int n;
    ussr_value_t keys[BT_MAX + 1];
    ussr_value_t vals[BT_MAX + 1];          /* leaves */
    struct bt_node *kids[BT_MAX + 2];       /* internal nodes */
    struct bt_node *next;                   /* leaves, in key order */
    struct bt_node *prev;
} bt_node;

struct ussr_btree_t
{
    bt_node *root;
    size_t count;
    size_t refcount;

    /* Mark-sweep GC registry linkage (gc.c). */
    unsigned char gc_mark;
    struct ussr_btree_t *gc_next;
};

/* ------------------------------------------------------------- */
/* keys                                                            */
/* ------------------------------------------------------------- */

int ussr_btree_key_ok(const ussr_value_t *key)
{
    return key != NULL &&
           (key->type == USSR_INTEGER || key->type == USSR_REAL ||
            (key->type == USSR_STRING && key->data.string != NULL));
}

int ussr_btree_key_compare(const ussr_value_t *a, const ussr_value_t *b)
{
    int as = a->type == USSR_STRING;
    int bs = b->type == USSR_STRING;

    if (as != bs)
        return as ? 1 : -1;                 /* numbers first */
    if (as)
    {
        int c = strcmp(a->data.string, b->data.string);
        return c < 0 ? -1 : c > 0;
    }
    if (a->type == USSR_INTEGER && b->type == USSR_INTEGER)
        return a->data.integer < b->data.integer ? -1
             : a->data.integer > b->data.integer;
    {
        double x = a->type == USSR_REAL ? a->data.real
                                        : (double)a->data.integer;
        double y = b->type == USSR_REAL ? b->data.real
                                        : (double)b->data.integer;
        return x < y ? -1 : x > y;
    }
}

/* The number of keys of node that are <= key: in an internal node, the
 * child to descend into. */
static int bt_upper(const bt_node *node, const ussr_value_t *key)
{
    int lo = 0;
    int hi = node->n;

    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (ussr_btree_key_compare(&node->keys[mid], key) <= 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

/* The number of keys of node that are < key: in a leaf, where key is or
 * would go. */
static int bt_lower(const bt_node *node, const ussr_value_t *key)
{
    int lo = 0;
    int hi = node->n;

    while (lo < hi)
    {
        int mid = (lo + hi) / 2;
        if (ussr_btree_key_compare(&node->keys[mid], key) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

static bt_node *bt_leaf_for(const ussr_btree_t *tree, const ussr_value_t *key)
{
    bt_node *node = tree->root;

    while (node != NULL && !node->leaf)
        node = node->kids[bt_upper(node, key)];
    return node;
}

/* ------------------------------------------------------------- */
/* nodes                                                           */
/* ------------------------------------------------------------- */

static bt_node *bt_node_new(int leaf)
{
    bt_node *node = calloc(1, sizeof(*node));

    if (node != NULL)
        node->leaf = leaf;
    return node;
}

/* release: 1 releases every value, 0 only the scalar ones (the GC
 * sweep frees the objects itself). */
static void bt_free(bt_node *node, int release)
{
    int i;

    if (node == NULL)
        return;
    for (i = 0; i < node->n; ++i)
        ussr_value_free(&node->keys[i]);
    if (node->leaf)
    {
        for (i = 0; i < node->n; ++i)
        {
            ussr_value_t *v = &node->vals[i];
            if (release || v->type == USSR_STRING)
                ussr_value_free(v);
        }
    }
    else
    {
        for (i = 0; i <= node->n; ++i)
            bt_free(node->kids[i], release);
    }
    free(node);
}

/* Moves count keys (and values or children) of a node within or between
 * arrays: plain memmove, the references move with them. */
static void bt_move_keys(ussr_value_t *dst, ussr_value_t *src, int count)
{
    if (count > 0)
        memmove(dst, src, (size_t)count * sizeof(*dst));
}

static void bt_move_kids(bt_node **dst, bt_node **src, int count)
{
    if (count > 0)
        memmove(dst, src, (size_t)count * sizeof(*dst));
}

/* ------------------------------------------------------------- */
/* the tree                                                        */
/* ------------------------------------------------------------- */

static ussr_btree_t *g_gc_btrees = NULL;

ussr_btree_t *ussr_btree_create(void)
{
    ussr_btree_t *tree = calloc(1, sizeof(*tree));

    if (tree == NULL)
        return NULL;
    tree->refcount = 1;
    tree->gc_next = g_gc_btrees;
    g_gc_btrees = tree;
    return tree;
}

void ussr_btree_gc_unregister(ussr_btree_t *tree)
{
    ussr_btree_t **link = &g_gc_btrees;

    while (*link != NULL)
    {
        if (*link == tree)
        {
            *link = tree->gc_next;
            tree->gc_next = NULL;
            return;
        }
        link = &(*link)->gc_next;
    }
}

void ussr_btree_gc_drop_ref(ussr_btree_t *tree)
{
    if (tree != NULL && tree->refcount > 0)
        tree->refcount--;
}

void ussr_btree_retain(ussr_btree_t *tree)
{
    if (tree != NULL)
        tree->refcount++;
}

void ussr_btree_release(ussr_btree_t *tree)
{
    if (tree == NULL || --tree->refcount > 0)
        return;
    ussr_btree_gc_unregister(tree);
    bt_free(tree->root, 1);
    free(tree);
}

void ussr_btree_destroy_raw(ussr_btree_t *tree)
{
    if (tree == NULL)
        return;
    bt_free(tree->root, 0);
    free(tree);
}

size_t ussr_btree_count(const ussr_btree_t *tree)
{
    return tree != NULL ? tree->count : 0;
}

ussr_btree_t *ussr_btree_gc_list(void)
{
    return g_gc_btrees;
}

ussr_btree_t *ussr_btree_gc_next(const ussr_btree_t *tree)
{
    return tree->gc_next;
}

int ussr_btree_gc_marked(const ussr_btree_t *tree)
{
    return tree->gc_mark;
}

void ussr_btree_gc_set_mark(ussr_btree_t *tree, int mark)
{
    tree->gc_mark = (unsigned char)mark;
}

void ussr_btree_each_value(ussr_btree_t *tree, void (*fn)(ussr_value_t *))
{
    bt_node *node = tree->root;
    int i;

    while (node != NULL && !node->leaf)
        node = node->kids[0];
    for (; node != NULL; node = node->next)
        for (i = 0; i < node->n; ++i)
            fn(&node->vals[i]);
}

int ussr_btree_each(const ussr_btree_t *tree,
                    int (*fn)(const ussr_value_t *, const ussr_value_t *,
                              void *),
                    void *ctx)
{
    bt_node *node = tree->root;
    int i;
    int status;

    while (node != NULL && !node->leaf)
        node = node->kids[0];
    for (; node != NULL; node = node->next)
        for (i = 0; i < node->n; ++i)
            if ((status = fn(&node->keys[i], &node->vals[i], ctx)) != 0)
                return status;
    return 0;
}

const ussr_value_t *ussr_btree_get(const ussr_btree_t *tree,
                                   const ussr_value_t *key)
{
    bt_node *leaf = bt_leaf_for(tree, key);
    int i;

    if (leaf == NULL)
        return NULL;
    i = bt_lower(leaf, key);
    if (i < leaf->n && ussr_btree_key_compare(&leaf->keys[i], key) == 0)
        return &leaf->vals[i];
    return NULL;
}

/* Inserts into the subtree of node. When node overflows it splits:
 * *right is the new right half and *sep the first key of it. -1 when
 * memory runs out. */
static int bt_insert(ussr_btree_t *tree, bt_node *node,
                     const ussr_value_t *key, const ussr_value_t *value,
                     bt_node **right, ussr_value_t *sep)
{
    int i;
    int half;
    bt_node *r;

    *right = NULL;
    if (node->leaf)
    {
        i = bt_lower(node, key);
        if (i < node->n && ussr_btree_key_compare(&node->keys[i], key) == 0)
        {
            ussr_value_t v = ussr_value_copy(value);
            ussr_value_free(&node->vals[i]);
            node->vals[i] = v;
            return 0;
        }
        bt_move_keys(&node->keys[i + 1], &node->keys[i], node->n - i);
        bt_move_keys(&node->vals[i + 1], &node->vals[i], node->n - i);
        node->keys[i] = ussr_value_copy(key);
        node->vals[i] = ussr_value_copy(value);
        node->n++;
        tree->count++;
        if (node->n <= BT_MAX)
            return 0;
        r = bt_node_new(1);
        if (r == NULL)
            return -1;
        half = node->n / 2;
        r->n = node->n - half;
        bt_move_keys(r->keys, &node->keys[half], r->n);
        bt_move_keys(r->vals, &node->vals[half], r->n);
        node->n = half;
        r->next = node->next;
        r->prev = node;
        if (node->next != NULL)
            node->next->prev = r;
        node->next = r;
        *sep = ussr_value_copy(&r->keys[0]);
        *right = r;
        return 0;
    }

    i = bt_upper(node, key);
    {
        bt_node *kid_right;
        ussr_value_t kid_sep;

        if (bt_insert(tree, node->kids[i], key, value, &kid_right,
                      &kid_sep) != 0)
            return -1;
        if (kid_right == NULL)
            return 0;
        bt_move_keys(&node->keys[i + 1], &node->keys[i], node->n - i);
        bt_move_kids(&node->kids[i + 2], &node->kids[i + 1], node->n - i);
        node->keys[i] = kid_sep;
        node->kids[i + 1] = kid_right;
        node->n++;
    }
    if (node->n <= BT_MAX)
        return 0;
    /* the middle key moves up; the halves keep the keys on either side */
    r = bt_node_new(0);
    if (r == NULL)
        return -1;
    half = node->n / 2;
    *sep = node->keys[half];
    r->n = node->n - half - 1;
    bt_move_keys(r->keys, &node->keys[half + 1], r->n);
    bt_move_kids(r->kids, &node->kids[half + 1], r->n + 1);
    node->n = half;
    *right = r;
    return 0;
}

int ussr_btree_put(ussr_btree_t *tree, const ussr_value_t *key,
                   const ussr_value_t *value)
{
    bt_node *right;
    ussr_value_t sep;

    if (!ussr_btree_key_ok(key))
        return -1;
    if (tree->root == NULL)
    {
        tree->root = bt_node_new(1);
        if (tree->root == NULL)
            return -1;
    }
    if (bt_insert(tree, tree->root, key, value, &right, &sep) != 0)
        return -1;
    if (right != NULL)
    {
        bt_node *root = bt_node_new(0);
        if (root == NULL)
            return -1;
        root->n = 1;
        root->keys[0] = sep;
        root->kids[0] = tree->root;
        root->kids[1] = right;
        tree->root = root;
    }
    return 0;
}

/* Child i of node has fewer than BT_MIN keys: borrow one from a sibling
 * that can spare it, else merge it with a sibling. */
static void bt_fix(bt_node *node, int i)
{
    bt_node *c = node->kids[i];
    bt_node *l = i > 0 ? node->kids[i - 1] : NULL;
    bt_node *r = i < node->n ? node->kids[i + 1] : NULL;

    if (l != NULL && l->n > BT_MIN)
    {
        bt_move_keys(&c->keys[1], &c->keys[0], c->n);
        if (c->leaf)
        {
            bt_move_keys(&c->vals[1], &c->vals[0], c->n);
            c->keys[0] = l->keys[l->n - 1];
            c->vals[0] = l->vals[l->n - 1];
            ussr_value_free(&node->keys[i - 1]);
            node->keys[i - 1] = ussr_value_copy(&c->keys[0]);
        }
        else
        {
            bt_move_kids(&c->kids[1], &c->kids[0], c->n + 1);
            c->keys[0] = node->keys[i - 1];
            c->kids[0] = l->kids[l->n];
            node->keys[i - 1] = l->keys[l->n - 1];
        }
        l->n--;
        c->n++;
        return;
    }
    if (r != NULL && r->n > BT_MIN)
    {
        if (c->leaf)
        {
            c->keys[c->n] = r->keys[0];
            c->vals[c->n] = r->vals[0];
            bt_move_keys(&r->keys[0], &r->keys[1], r->n - 1);
            bt_move_keys(&r->vals[0], &r->vals[1], r->n - 1);
            ussr_value_free(&node->keys[i]);
            node->keys[i] = ussr_value_copy(&r->keys[0]);
        }
        else
        {
            c->keys[c->n] = node->keys[i];
            c->kids[c->n + 1] = r->kids[0];
            node->keys[i] = r->keys[0];
            bt_move_keys(&r->keys[0], &r->keys[1], r->n - 1);
            bt_move_kids(&r->kids[0], &r->kids[1], r->n);
        }
        r->n--;
        c->n++;
        return;
    }
    /* merge kids[j] and kids[j + 1] into kids[j] */
    {
        int j = l != NULL ? i - 1 : i;
        bt_node *a = node->kids[j];
        bt_node *b = node->kids[j + 1];

        if (a->leaf)
        {
            bt_move_keys(&a->keys[a->n], b->keys, b->n);
            bt_move_keys(&a->vals[a->n], b->vals, b->n);
            a->n += b->n;
            a->next = b->next;
            if (b->next != NULL)
                b->next->prev = a;
            ussr_value_free(&node->keys[j]);
        }
        else
        {
            a->keys[a->n] = node->keys[j];
            bt_move_keys(&a->keys[a->n + 1], b->keys, b->n);
            bt_move_kids(&a->kids[a->n + 1], b->kids, b->n + 1);
            a->n += b->n + 1;
        }
        free(b);
        bt_move_keys(&node->keys[j], &node->keys[j + 1], node->n - j - 1);
        bt_move_kids(&node->kids[j + 1], &node->kids[j + 2], node->n - j - 1);
        node->n--;
    }
}

static int bt_delete(ussr_btree_t *tree, bt_node *node,
                     const ussr_value_t *key)
{
    int i;

    if (node->leaf)
    {
        i = bt_lower(node, key);
        if (i >= node->n ||
            ussr_btree_key_compare(&node->keys[i], key) != 0)
            return 0;
        ussr_value_free(&node->keys[i]);
        ussr_value_free(&node->vals[i]);
        bt_move_keys(&node->keys[i], &node->keys[i + 1], node->n - i - 1);
        bt_move_keys(&node->vals[i], &node->vals[i + 1], node->n - i - 1);
        node->n--;
        tree->count--;
        return 1;
    }
    i = bt_upper(node, key);
    if (!bt_delete(tree, node->kids[i], key))
        return 0;
    if (node->kids[i]->n < BT_MIN)
        bt_fix(node, i);
    return 1;
}

int ussr_btree_delete(ussr_btree_t *tree, const ussr_value_t *key)
{
    bt_node *root = tree->root;

    if (root == NULL || !ussr_btree_key_ok(key) ||
        !bt_delete(tree, root, key))
        return 0;
    if (!root->leaf && root->n == 0)
    {
        tree->root = root->kids[0];
        free(root);
    }
    else if (root->leaf && root->n == 0)
    {
        free(root);
        tree->root = NULL;
    }
    return 1;
}

/* ------------------------------------------------------------- */
/* positions                                                       */
/* ------------------------------------------------------------- */

typedef struct
{
    bt_node *leaf;
    int i;
} bt_pos;

/* The first key >= key (strict: > key). */
static bt_pos bt_seek(const ussr_btree_t *tree, const ussr_value_t *key,
                      int strict)
{
    bt_pos p;

    p.leaf = bt_leaf_for(tree, key);
    p.i = 0;
    if (p.leaf == NULL)
        return p;
    p.i = strict ? bt_upper(p.leaf, key) : bt_lower(p.leaf, key);
    while (p.leaf != NULL && p.i >= p.leaf->n)
    {
        p.leaf = p.leaf->next;
        p.i = 0;
    }
    return p;
}

/* The last key <= key (strict: < key). */
static bt_pos bt_seek_back(const ussr_btree_t *tree, const ussr_value_t *key,
                           int strict)
{
    bt_pos p;

    p.leaf = bt_leaf_for(tree, key);
    p.i = -1;
    if (p.leaf == NULL)
        return p;
    p.i = (strict ? bt_lower(p.leaf, key) : bt_upper(p.leaf, key)) - 1;
    while (p.leaf != NULL && p.i < 0)
    {
        p.leaf = p.leaf->prev;
        if (p.leaf != NULL)
            p.i = p.leaf->n - 1;
    }
    return p;
}

static bt_pos bt_first(const ussr_btree_t *tree)
{
    bt_pos p;

    p.leaf = tree->root;
    p.i = 0;
    while (p.leaf != NULL && !p.leaf->leaf)
        p.leaf = p.leaf->kids[0];
    if (p.leaf != NULL && p.leaf->n == 0)
        p.leaf = NULL;
    return p;
}

static bt_pos bt_last(const ussr_btree_t *tree)
{
    bt_pos p;

    p.leaf = tree->root;
    while (p.leaf != NULL && !p.leaf->leaf)
        p.leaf = p.leaf->kids[p.leaf->n];
    p.i = p.leaf != NULL ? p.leaf->n - 1 : -1;
    if (p.i < 0)
        p.leaf = NULL;
    return p;
}

/* ------------------------------------------------------------- */
/* the commands                                                    */
/* ------------------------------------------------------------- */

static int bt_usage(const char *text)
{
    fprintf(stderr, "USSR: %s\n", text);
    return -1;
}

/* Evaluates arguments[0] as a tree into *holder (to free later). */
static int bt_tree_arg(const char *command, ussr_argument_t *arguments,
                       ussr_value_t *holder, ussr_btree_t **tree)
{
    if (ussr_argument_evaluate(&arguments[0], holder) != 0)
        return -1;
    if (holder->type != USSR_BTREE || holder->data.btree == NULL)
    {
        fprintf(stderr, "USSR: %s expects a btree first\n", command);
        ussr_value_free(holder);
        return -1;
    }
    *tree = holder->data.btree;
    return 0;
}

static int bt_key_arg(const char *command, ussr_argument_t *argument,
                      ussr_value_t *key)
{
    if (ussr_argument_evaluate(argument, key) != 0)
        return -1;
    if (!ussr_btree_key_ok(key))
    {
        fprintf(stderr, "USSR: %s: btree keys are numbers or strings\n",
                command);
        ussr_value_free(key);
        return -1;
    }
    return 0;
}

static ussr_value_t bt_key_out(bt_pos p)
{
    if (p.leaf == NULL)
        return ussr_null();
    return ussr_value_copy(&p.leaf->keys[p.i]);
}

/* btrange and btkeys */
static int bt_range(const char *command, int pairs, ussr_btree_t *tree,
                    ussr_argument_t *arguments, size_t argument_count,
                    ussr_value_t *out_result)
{
    ussr_value_t bound[2];
    ussr_int_t limit = -1;
    ussr_int_t taken = 0;
    ussr_vector_t *out;
    bt_pos p;
    size_t k;
    int status = 0;

    bound[0] = ussr_null();
    bound[1] = ussr_null();
    for (k = 1; k < argument_count && k <= 3; ++k)
    {
        ussr_value_t v;

        if (ussr_argument_evaluate(&arguments[k], &v) != 0)
            status = -1;
        else if (k < 3 && v.type != USSR_NULL && !ussr_btree_key_ok(&v))
        {
            fprintf(stderr, "USSR: %s: bounds are keys or null\n", command);
            ussr_value_free(&v);
            status = -1;
        }
        else if (k == 3)
        {
            if (v.type == USSR_INTEGER)
                limit = v.data.integer;
            else if (v.type != USSR_NULL)
            {
                fprintf(stderr, "USSR: %s: the limit is an integer\n",
                        command);
                status = -1;
            }
            ussr_value_free(&v);
        }
        else
            bound[k - 1] = v;
    }
    if (status != 0)
    {
        ussr_value_free(&bound[0]);
        ussr_value_free(&bound[1]);
        return -1;
    }
    out = ussr_vector_create(NULL);
    if (out == NULL)
        status = -1;
    p = bound[0].type == USSR_NULL ? bt_first(tree)
                                   : bt_seek(tree, &bound[0], 0);
    while (status == 0 && p.leaf != NULL && (limit < 0 || taken < limit))
    {
        const ussr_value_t *key = &p.leaf->keys[p.i];

        if (bound[1].type != USSR_NULL &&
            ussr_btree_key_compare(key, &bound[1]) >= 0)
            break;
        if (pairs)
        {
            ussr_vector_t *pair = ussr_vector_create(NULL);
            ussr_value_t pv;

            if (pair == NULL ||
                ussr_vector_push(pair, *key) != 0 ||
                ussr_vector_push(pair, p.leaf->vals[p.i]) != 0)
                status = -1;
            pv.type = USSR_VECTOR;
            pv.data.vector = pair;
            if (status == 0 && ussr_vector_push(out, pv) != 0)
                status = -1;
            ussr_vector_release(pair);
        }
        else if (ussr_vector_push(out, *key) != 0)
            status = -1;
        ++taken;
        if (++p.i >= p.leaf->n)
        {
            p.leaf = p.leaf->next;
            p.i = 0;
        }
    }
    ussr_value_free(&bound[0]);
    ussr_value_free(&bound[1]);
    if (status != 0)
    {
        ussr_vector_release(out);
        return -1;
    }
    out_result->type = USSR_VECTOR;
    out_result->data.vector = out;
    return 0;
}

static int bt_command(const char *command, ussr_argument_t *arguments,
                      size_t argument_count, ussr_value_t *out_result)
{
    ussr_value_t holder;
    ussr_value_t key;
    ussr_btree_t *tree;
    int status = 0;

    if (strcmp(command, "btree") == 0)
    {
        if (argument_count != 0)
            return bt_usage("btree(t): takes no arguments");
        tree = ussr_btree_create();
        if (tree == NULL)
            return -1;
        out_result->type = USSR_BTREE;
        out_result->data.btree = tree;
        return 0;
    }

    if (argument_count < 1)
    {
        fprintf(stderr, "USSR: %s expects a btree first\n", command);
        return -1;
    }
    if (bt_tree_arg(command, arguments, &holder, &tree) != 0)
        return -1;

    if (strcmp(command, "btput") == 0)
    {
        ussr_value_t value;

        if (argument_count != 3)
            status = bt_usage("btput(t): t key value");
        else if (bt_key_arg(command, &arguments[1], &key) != 0)
            status = -1;
        else
        {
            if (ussr_argument_evaluate(&arguments[2], &value) != 0)
                status = -1;
            else
            {
                status = ussr_btree_put(tree, &key, &value);
                ussr_value_free(&value);
            }
            ussr_value_free(&key);
        }
        if (status == 0)
            *out_result = ussr_value_copy(&holder);
    }
    else if (strcmp(command, "btget") == 0 || strcmp(command, "bthas") == 0)
    {
        int has = strcmp(command, "bthas") == 0;

        if (argument_count != 2 && (has || argument_count != 3))
            status = bt_usage(has ? "bthas(b): t key"
                                  : "btget(v): t key [default]");
        else if (bt_key_arg(command, &arguments[1], &key) != 0)
            status = -1;
        else
        {
            const ussr_value_t *found = ussr_btree_get(tree, &key);

            if (has)
                *out_result = ussr_boolean(found != NULL);
            else if (found != NULL)
                *out_result = ussr_value_copy(found);
            else if (argument_count == 3)
                status = ussr_argument_evaluate(&arguments[2], out_result);
            else
                *out_result = ussr_null();
            ussr_value_free(&key);
        }
    }
    else if (strcmp(command, "btdel") == 0)
    {
        if (argument_count != 2)
            status = bt_usage("btdel(b): t key");
        else if (bt_key_arg(command, &arguments[1], &key) != 0)
            status = -1;
        else
        {
            *out_result = ussr_boolean(ussr_btree_delete(tree, &key));
            ussr_value_free(&key);
        }
    }
    else if (strcmp(command, "btlen") == 0)
    {
        if (argument_count != 1)
            status = bt_usage("btlen(n): t");
        else
            *out_result = ussr_integer((ussr_int_t)tree->count);
    }
    else if (strcmp(command, "btfirst") == 0 ||
             strcmp(command, "btlast") == 0)
    {
        if (argument_count != 1)
            status = bt_usage("btfirst(k): t / btlast(k): t");
        else
            *out_result = bt_key_out(command[2] == 'f' ? bt_first(tree)
                                                       : bt_last(tree));
    }
    else if (strcmp(command, "btfloor") == 0 ||
             strcmp(command, "btceil") == 0 ||
             strcmp(command, "btprev") == 0 ||
             strcmp(command, "btnext") == 0)
    {
        if (argument_count != 2)
        {
            fprintf(stderr, "USSR: %s(k): t key\n", command);
            status = -1;
        }
        else if (bt_key_arg(command, &arguments[1], &key) != 0)
            status = -1;
        else
        {
            bt_pos p;

            if (strcmp(command, "btfloor") == 0)
                p = bt_seek_back(tree, &key, 0);
            else if (strcmp(command, "btprev") == 0)
                p = bt_seek_back(tree, &key, 1);
            else if (strcmp(command, "btceil") == 0)
                p = bt_seek(tree, &key, 0);
            else
                p = bt_seek(tree, &key, 1);
            *out_result = bt_key_out(p);
            ussr_value_free(&key);
        }
    }
    else if (strcmp(command, "btrange") == 0 ||
             strcmp(command, "btkeys") == 0)
    {
        if (argument_count > 4)
        {
            fprintf(stderr, "USSR: %s(v): t [lo [hi [max]]]\n", command);
            status = -1;
        }
        else
            status = bt_range(command, command[2] == 'r', tree, arguments,
                              argument_count, out_result);
    }
    else
        status = 1;                         /* not ours */

    ussr_value_free(&holder);
    return status;
}

int ussr_btree_dispatch(const char *command, ussr_argument_t *arguments,
                        size_t argument_count, ussr_value_t *out_result)
{
    int status;

    if (strncmp(command, "bt", 2) != 0)
        return 0;
    if (strcmp(command, "btree") != 0 && strcmp(command, "btput") != 0 &&
        strcmp(command, "btget") != 0 && strcmp(command, "bthas") != 0 &&
        strcmp(command, "btdel") != 0 && strcmp(command, "btlen") != 0 &&
        strcmp(command, "btfirst") != 0 && strcmp(command, "btlast") != 0 &&
        strcmp(command, "btfloor") != 0 && strcmp(command, "btceil") != 0 &&
        strcmp(command, "btprev") != 0 && strcmp(command, "btnext") != 0 &&
        strcmp(command, "btrange") != 0 && strcmp(command, "btkeys") != 0)
        return 0;
    status = bt_command(command, arguments, argument_count, out_result);
    return status == 0 ? 1 : -1;
}
