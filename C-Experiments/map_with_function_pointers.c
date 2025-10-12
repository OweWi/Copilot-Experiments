/*
    main.c
    A compact C99 implementation of a generic std::map-like container using an AVL tree.

    - Generic keys/values as void*
    - User supplies: compare function, optional dup/free functions for keys and values
    - Provides: create, destroy, insert (no overwrite), put (insert or assign), find, erase,
        size, begin/end iteration, next/prev iteration.

    Compile:
        gcc -std=c99 -O2 main.c -o main

    Example usage in main(): string keys, int values.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* map types and callbacks */
typedef int (*map_cmp_fn)(const void *a, const void *b);
typedef void *(*map_dup_fn)(const void *x);
typedef void (*map_free_fn)(void *x);

/* Forward declarations */
struct map_node;

/* Iterator type is just a node pointer */
typedef struct map_node *map_iter_t;

typedef struct map
{
    struct map_node *root;
    size_t size;
    map_cmp_fn cmp;
    map_dup_fn key_dup;
    map_free_fn key_free;
    map_dup_fn val_dup;
    map_free_fn val_free;
} map_t;

typedef struct map_node
{
    void *key;
    void *value;
    struct map_node *left;
    struct map_node *right;
    struct map_node *parent;
    int height;
} map_node_t;

/* Utility helpers */

/**
 * @brief Return the height of a node (0 if NULL).
 *
 * @param n Pointer to map_node_t or NULL.
 * @return int Height (>=0).
 */
static int node_height(map_node_t *n) { return n ? n->height : 0; }

/**
 * @brief Recompute and set the height field for node n based on children.
 *
 * If n is NULL the function does nothing.
 *
 * @param n Pointer to map_node_t whose height is updated.
 */
static void update_height(map_node_t *n)
{
    if (n)
    {
        int hl = node_height(n->left);
        int hr = node_height(n->right);
        n->height = (hl > hr ? hl : hr) + 1;
    }
}

/* Create and destroy nodes */

/**
 * @brief Allocate and initialize a new map node.
 *
 * The function uses the map's key_dup/val_dup callbacks if provided to
 * duplicate the key and value; otherwise it stores the pointers as-is.
 *
 * @param m Pointer to the owning map_t.
 * @param key Pointer to the key to store (may be NULL depending on usage).
 * @param value Pointer to the value to store (may be NULL).
 * @return map_node_t* Pointer to the newly allocated node, or NULL on OOM.
 */
static map_node_t *node_new(map_t *m, void *key, void *value)
{
    map_node_t *n = (map_node_t *)malloc(sizeof(map_node_t));
    if (!n)
        return NULL;
    n->left = n->right = n->parent = NULL;
    n->height = 1;
    /* store duplicated key/value if dup functions provided, else store pointers as-is */
    n->key = (m->key_dup ? m->key_dup(key) : key);
    n->value = (m->val_dup ? m->val_dup(value) : value);
    return n;
}

/**
 * @brief Free a node and its stored key/value resources (via callbacks).
 *
 * This frees the memory for the key and value using the map's key_free/val_free
 * callbacks if they are non-NULL, then frees the node itself.
 *
 * @param m Pointer to owning map_t.
 * @param n Pointer to node to free (NULL safe).
 */
static void node_free(map_t *m, map_node_t *n)
{
    if (!n)
        return;
    if (m->key_free && n->key)
        m->key_free(n->key);
    if (m->val_free && n->value)
        m->val_free(n->value);
    free(n);
}

/* Rotate helpers return new subtree root and maintain parent pointers */

/**
 * @brief Perform a right rotation on subtree rooted at y.
 *
 * After rotation this function returns the new root (x). Parent pointers
 * and heights are adjusted appropriately.
 *
 * @param y Root of subtree to rotate (must be non-NULL and have left child).
 * @return map_node_t* New root of the rotated subtree.
 */
static map_node_t *rotate_right(map_node_t *y)
{
    map_node_t *x = y->left;
    map_node_t *T2 = x->right;

    x->right = y;
    y->left = T2;

    if (T2)
        T2->parent = y;

    x->parent = y->parent;
    y->parent = x;

    update_height(y);
    update_height(x);
    return x;
}

/**
 * @brief Perform a left rotation on subtree rooted at x.
 *
 * After rotation this function returns the new root (y). Parent pointers
 * and heights are adjusted appropriately.
 *
 * @param x Root of subtree to rotate (must be non-NULL and have right child).
 * @return map_node_t* New root of the rotated subtree.
 */
static map_node_t *rotate_left(map_node_t *x)
{
    map_node_t *y = x->right;
    map_node_t *T2 = y->left;

    y->left = x;
    x->right = T2;

    if (T2)
        T2->parent = x;

    y->parent = x->parent;
    x->parent = y;

    update_height(x);
    update_height(y);
    return y;
}

/* Helper to replace parent's child pointer with new child (or set root if parent NULL) */

/**
 * @brief Replace parent's pointer from old_child to new_child.
 *
 * If parent is NULL, new_child becomes the map root. Parent pointers are
 * updated for new_child if non-NULL.
 *
 * @param m Pointer to the map owning the tree.
 * @param parent Parent node whose child pointer should be updated (may be NULL).
 * @param old_child The previous child pointer (used to decide left/right).
 * @param new_child The new child to attach (may be NULL).
 */
static void set_child(map_t *m, map_node_t *parent, map_node_t *old_child, map_node_t *new_child)
{
    if (!parent)
    {
        m->root = new_child;
        if (new_child)
            new_child->parent = NULL;
    }
    else
    {
        if (parent->left == old_child)
            parent->left = new_child;
        else
            parent->right = new_child;
        if (new_child)
            new_child->parent = parent;
    }
}

/* Rebalance at node and return the node that occupies this position after rebalancing */

/**
 * @brief Rebalance the subtree rooted at node if needed and return new subroot.
 *
 * This function computes the AVL balance factor, performs the necessary
 * single/double rotations (LL, LR, RR, RL), updates heights and parent
 * pointers, and returns the node that should occupy the original node's
 * position after rebalancing.
 *
 * @param m Pointer to the map (unused for logic but kept for symmetry).
 * @param node Root of subtree to rebalance (must be non-NULL).
 * @return map_node_t* New root of the rebalanced subtree (may be same as node).
 */
static map_node_t *rebalance_at(map_t *m, map_node_t *node)
{
    update_height(node);
    int balance = node_height(node->left) - node_height(node->right);

    if (balance > 1)
    {
        /* left heavy */
        if (node_height(node->left->left) >= node_height(node->left->right))
        {
            /* LL case */
            map_node_t *new_root = rotate_right(node);
            return new_root;
        }
        else
        {
            /* LR case */
            node->left = rotate_left(node->left);
            if (node->left)
                node->left->parent = node;
            map_node_t *new_root = rotate_right(node);
            return new_root;
        }
    }
    else if (balance < -1)
    {
        /* right heavy */
        if (node_height(node->right->right) >= node_height(node->right->left))
        {
            /* RR case */
            map_node_t *new_root = rotate_left(node);
            return new_root;
        }
        else
        {
            /* RL case */
            node->right = rotate_right(node->right);
            if (node->right)
                node->right->parent = node;
            map_node_t *new_root = rotate_left(node);
            return new_root;
        }
    }
    return node;
}

/* Public API */

/**
 * @brief Create a new map instance.
 *
 * The caller must provide a compare function. Optional key/value duplicate
 * and free callbacks may be supplied; if NULL, keys/values are stored as
 * provided and not freed by the map.
 *
 * @param cmp Compare callback; must return negative/zero/positive like strcmp.
 * @param key_dup Optional key duplication callback (may be NULL).
 * @param key_free Optional key free callback (may be NULL).
 * @param val_dup Optional value duplication callback (may be NULL).
 * @param val_free Optional value free callback (may be NULL).
 * @return map_t* Newly allocated map or NULL on OOM or NULL cmp.
 */
map_t *map_create(map_cmp_fn cmp,
                  map_dup_fn key_dup, map_free_fn key_free,
                  map_dup_fn val_dup, map_free_fn val_free)
{
    if (!cmp)
        return NULL;
    map_t *m = (map_t *)malloc(sizeof(map_t));
    if (!m)
        return NULL;
    m->root = NULL;
    m->size = 0;
    m->cmp = cmp;
    m->key_dup = key_dup;
    m->key_free = key_free;
    m->val_dup = val_dup;
    m->val_free = val_free;
    return m;
}

/* Internal: find node by key */

/**
 * @brief Find the node that contains the given key.
 *
 * Uses the map's compare callback for key ordering.
 *
 * @param m Pointer to map_t (must be non-NULL).
 * @param key Pointer to key to search for.
 * @return map_node_t* Node containing the key, or NULL if not found.
 */
static map_node_t *find_node(map_t *m, const void *key)
{
    map_node_t *cur = m->root;
    while (cur)
    {
        int c = m->cmp(key, cur->key);
        if (c == 0)
            return cur;
        cur = (c < 0) ? cur->left : cur->right;
    }
    return NULL;
}

/**
 * @brief Insert a new key/value pair into the map without overwriting.
 *
 * If the key already exists the function returns 0 and does not change the map.
 * On success returns 1. Returns -1 on memory allocation failure or invalid map.
 *
 * @param m Pointer to map_t.
 * @param key Pointer to key to insert.
 * @param value Pointer to value to insert.
 * @return int 1 if inserted, 0 if key existed, -1 on OOM or invalid map.
 */
int map_insert(map_t *m, void *key, void *value)
{
    if (!m)
        return -1;
    if (!m->root)
    {
        map_node_t *n = node_new(m, key, value);
        if (!n)
            return -1;
        m->root = n;
        m->size = 1;
        return 1;
    }

    /* Find insertion point (or existing) */
    map_node_t *parent = NULL;
    map_node_t *cur = m->root;
    int cmpv = 0;
    while (cur)
    {
        cmpv = m->cmp(key, cur->key);
        if (cmpv == 0)
        {
            /* exists: do not overwrite */
            return 0;
        }
        parent = cur;
        cur = (cmpv < 0) ? cur->left : cur->right;
    }

    /* Attach new node */
    map_node_t *n = node_new(m, key, value);
    if (!n)
        return -1;
    n->parent = parent;
    if (cmpv < 0)
        parent->left = n;
    else
        parent->right = n;
    m->size++;

    /* Rebalance walking up */
    map_node_t *p = n->parent;
    while (p)
    {
        map_node_t *old_p = p;
        map_node_t *new_subroot = rebalance_at(m, p);
        /* update parent's child link to new_subroot */
        set_child(m, new_subroot->parent, old_p, new_subroot);
        p = new_subroot->parent;
    }
    /* ensure root parent is NULL */
    if (m->root && m->root->parent)
        m->root->parent = NULL;
    return 1;
}

/**
 * @brief Insert or replace a key/value pair.
 *
 * If the key exists the stored value is replaced (old value freed if val_free
 * callback present) and the function returns 2. If the key was inserted returns 1.
 * Returns -1 on OOM or invalid map.
 *
 * @param m Pointer to map_t.
 * @param key Pointer to key.
 * @param value Pointer to value.
 * @return int 1 if inserted, 2 if replaced, -1 on OOM or invalid map.
 */
int map_put(map_t *m, void *key, void *value)
{
    if (!m)
        return -1;
    map_node_t *existing = find_node(m, key);
    if (existing)
    {
        /* replace value */
        if (m->val_free)
            m->val_free(existing->value);
        existing->value = (m->val_dup ? m->val_dup(value) : value);
        return 2;
    }
    return map_insert(m, key, value);
}

/**
 * @brief Find the value pointer associated with a key.
 *
 * @param m Pointer to map_t.
 * @param key Pointer to search key.
 * @return void* Stored value pointer if found, NULL if not found.
 */
void *map_find(map_t *m, const void *key)
{
    map_node_t *n = find_node(m, key);
    return n ? n->value : NULL;
}

/* Minimum node in subtree */

/**
 * @brief Return the node with minimum key in the subtree.
 *
 * @param n Subtree root (may be NULL).
 * @return map_node_t* Node with minimum key or NULL if n is NULL.
 */
static map_node_t *subtree_min(map_node_t *n)
{
    if (!n)
        return NULL;
    while (n->left)
        n = n->left;
    return n;
}

/* Remove a node given pointer. Returns pointer to parent where balancing continues */

/**
 * @brief Erase the given node from the tree and free its resources.
 *
 * If the node has two children it swaps with the inorder successor and deletes
 * that successor instead. Returns the parent node where balancing should continue.
 *
 * @param m Pointer to map_t.
 * @param n Node to erase (must be non-NULL).
 * @return map_node_t* Parent node to continue rebalancing from, or NULL.
 */
static map_node_t *erase_node(map_t *m, map_node_t *n)
{
    if (!n)
        return NULL;
    map_node_t *parent = NULL;

    if (n->left && n->right)
    {
        /* two children: swap with successor */
        map_node_t *suc = subtree_min(n->right);
        /* swap key/value pointers (do not call free/dup) */
        void *tkey = n->key;
        n->key = suc->key;
        suc->key = tkey;
        void *tval = n->value;
        n->value = suc->value;
        suc->value = tval;
        /* now delete suc (which has at most right child) */
        return erase_node(m, suc);
    }
    else
    {
        map_node_t *child = n->left ? n->left : n->right;
        parent = n->parent;
        /* detach n and link child in its place */
        if (parent == NULL)
        {
            /* deleting root */
            m->root = child;
            if (child)
                child->parent = NULL;
        }
        else
        {
            if (parent->left == n)
                parent->left = child;
            else
                parent->right = child;
            if (child)
                child->parent = parent;
        }
        /* free resources */
        node_free(m, n);
        m->size--;
        return parent;
    }
}

/**
 * @brief Erase an entry by key.
 *
 * Finds the node with the given key, removes it if present, rebalances the tree,
 * and returns 1 if an entry was erased or 0 if not found.
 *
 * @param m Pointer to map_t.
 * @param key Key to erase.
 * @return int 1 if erased, 0 if not found or invalid map.
 */
int map_erase(map_t *m, const void *key)
{
    if (!m)
        return 0;
    map_node_t *n = find_node(m, key);
    if (!n)
        return 0;
    map_node_t *p = erase_node(m, n);
    /* rebalance upwards */
    while (p)
    {
        map_node_t *old_p = p;
        map_node_t *new_subroot = rebalance_at(m, p);
        set_child(m, new_subroot->parent, old_p, new_subroot);
        p = new_subroot->parent;
    }
    if (m->root && m->root->parent)
        m->root->parent = NULL;
    return 1;
}

/* Size */

/**
 * @brief Return the number of elements stored in the map.
 *
 * @param m Pointer to map_t (may be NULL).
 * @return size_t Number of stored elements (0 if m is NULL).
 */
size_t map_size(map_t *m) { return m ? m->size : 0; }

/* Clear and destroy */

/**
 * @brief Recursively free all nodes in the subtree rooted at n.
 *
 * Invokes node_free for each node which will apply key/value free callbacks.
 *
 * @param m Pointer to map_t owning the nodes.
 * @param n Subtree root to free (may be NULL).
 */
static void free_subtree(map_t *m, map_node_t *n)
{
    if (!n)
        return;
    free_subtree(m, n->left);
    free_subtree(m, n->right);
    node_free(m, n);
}

/**
 * @brief Remove all entries from the map but keep the map structure.
 *
 * After this call the map is empty (size == 0, root == NULL).
 *
 * @param m Pointer to map_t.
 */
void map_clear(map_t *m)
{
    if (!m)
        return;
    free_subtree(m, m->root);
    m->root = NULL;
    m->size = 0;
}

/**
 * @brief Destroy the map and free all associated resources.
 *
 * Calls map_clear and then frees the map structure itself.
 *
 * @param m Pointer to map_t to destroy (NULL safe).
 */
void map_destroy(map_t *m)
{
    if (!m)
        return;
    map_clear(m);
    free(m);
}

/* Iteration: begin is smallest; end is NULL. next/prev provide in-order traversal */

/**
 * @brief Return iterator to the smallest (leftmost) element in the map.
 *
 * @param m Pointer to map_t.
 * @return map_iter_t Iterator (node pointer) to first element or NULL if map empty.
 */
map_iter_t map_begin(map_t *m) { return m ? subtree_min(m->root) : NULL; }

/**
 * @brief Return the end iterator for the map (always NULL).
 *
 * Provided for API symmetry with STL-like iteration.
 *
 * @param m Pointer to map_t (unused).
 * @return map_iter_t Always NULL.
 */
map_iter_t map_end(map_t *m)
{
    (void)m;
    return NULL;
}

/**
 * @brief Advance iterator to the next in-order node.
 *
 * If it has a right subtree, returns the leftmost node in that subtree;
 * otherwise walks up to the first ancestor where current is in left subtree.
 *
 * @param it Current iterator (may be NULL).
 * @return map_iter_t Next iterator or NULL if at end.
 */
map_iter_t map_next(map_iter_t it)
{
    if (!it)
        return NULL;
    if (it->right)
    {
        map_node_t *n = it->right;
        while (n->left)
            n = n->left;
        return n;
    }
    else
    {
        map_node_t *p = it->parent;
        map_node_t *cur = it;
        while (p && p->right == cur)
        {
            cur = p;
            p = p->parent;
        }
        return p;
    }
}

/**
 * @brief Move iterator to the previous in-order node.
 *
 * If it has a left subtree, returns the rightmost node in that subtree;
 * otherwise walks up to the first ancestor where current is in right subtree.
 *
 * @param it Current iterator (may be NULL).
 * @return map_iter_t Previous iterator or NULL if at begin.
 */
map_iter_t map_prev(map_iter_t it)
{
    if (!it)
        return NULL;
    if (it->left)
    {
        map_node_t *n = it->left;
        while (n->right)
            n = n->right;
        return n;
    }
    else
    {
        map_node_t *p = it->parent;
        map_node_t *cur = it;
        while (p && p->left == cur)
        {
            cur = p;
            p = p->parent;
        }
        return p;
    }
}

/* Access key/value from iterator */

/**
 * @brief Return pointer to key stored at iterator.
 *
 * @param it Iterator (node pointer).
 * @return void* Key pointer or NULL if it is NULL.
 */
void *map_iter_key(map_iter_t it) { return it ? it->key : NULL; }

/**
 * @brief Return pointer to value stored at iterator.
 *
 * @param it Iterator (node pointer).
 * @return void* Value pointer or NULL if it is NULL.
 */
void *map_iter_value(map_iter_t it) { return it ? it->value : NULL; }

/* ---------------- Example usage ---------------- */

/**
 * @brief Compare two C-strings using strcmp for map ordering.
 *
 * @param a First C-string.
 * @param b Second C-string.
 * @return int <0, 0, >0 like strcmp.
 */
static int cstr_cmp(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/**
 * @brief Duplicate a C-string using malloc.
 *
 * Caller must free returned pointer; used as key_dup in example.
 *
 * @param s C-string to duplicate (may be NULL).
 * @return void* Newly allocated string or NULL on OOM or NULL input.
 */
static void *cstr_dup(const void *s)
{
    if (!s)
        return NULL;
    size_t len = strlen((const char *)s);
    char *d = (char *)malloc(len + 1);
    if (!d)
        return NULL;
    memcpy(d, s, len + 1);
    return d;
}

/**
 * @brief Free a C-string allocated by cstr_dup.
 *
 * @param s Pointer returned by cstr_dup.
 */
static void cstr_free(void *s) { free(s); }

/**
 * @brief Duplicate an int value on the heap.
 *
 * Used as example val_dup; returns a newly allocated int containing *p.
 *
 * @param p Pointer to int to duplicate.
 * @return void* Allocated int pointer or NULL on OOM.
 */
static void *int_dup(const void *p)
{
    int *d = malloc(sizeof(int));
    if (!d)
        return NULL;
    *d = *(const int *)p;
    return d;
}

/**
 * @brief Free an int allocated by int_dup.
 *
 * @param p Pointer returned by int_dup.
 */
static void int_free(void *p) { free(p); }

int main(void)
{
    /* map from C string -> int (both copied) */
    map_t *m = map_create(cstr_cmp, cstr_dup, cstr_free, int_dup, int_free);
    if (!m)
        return 1;

    int v;

    v = 42;
    map_insert(m, "apple", &v);
    v = 7;
    map_insert(m, "orange", &v);
    v = 13;
    map_put(m, "banana", &v);
    v = 100;
    map_put(m, "apple", &v); /* replace apple's value */

    printf("map size: %zu\n", map_size(m));

    /* lookup */
    int *pv = map_find(m, "apple");
    if (pv)
        printf("apple -> %d\n", *pv);

    /* iterate in order */
    printf("in-order traversal:\n");
    for (map_iter_t it = map_begin(m); it != map_end(m); it = map_next(it))
    {
        printf("  %s -> %d\n", (char *)map_iter_key(it), *(int *)map_iter_value(it));
    }

    /* erase an element */
    map_erase(m, "orange");
    printf("after erase orange, size=%zu\n", map_size(m));

    /* clear and destroy */
    map_destroy(m);
    return 0;
}

/* ---- Example usage changed: uint32_t keys, function-pointer values ---- */

#include <inttypes.h> /* for PRIu32 if needed */
typedef void (*fp_t)(void);

/**
 * @brief Compare two uint32_t keys.
 *
 * Expects pointers to uint32_t values.
 *
 * @param a Pointer to first uint32_t.
 * @param b Pointer to second uint32_t.
 * @return int -1 if *a < *b, 0 if equal, 1 if *a > *b.
 */
static int u32_cmp(const void *a, const void *b)
{
    uint32_t va = *(const uint32_t *)a;
    uint32_t vb = *(const uint32_t *)b;
    return (va > vb) ? 1 : (va < vb) ? -1
                                     : 0;
}

/**
 * @brief Duplicate a uint32_t on the heap.
 *
 * Allocates and returns a pointer to a copied uint32_t. Caller must free.
 *
 * @param x Pointer to source uint32_t.
 * @return void* Allocated copy or NULL on OOM.
 */
static void *u32_dup(const void *x)
{
    if (!x)
        return NULL;
    uint32_t *d = malloc(sizeof(uint32_t));
    if (!d)
        return NULL;
    *d = *(const uint32_t *)x;
    return d;
}

/**
 * @brief Free a heap-allocated uint32_t.
 *
 * @param x Pointer returned by u32_dup.
 */
static void u32_free(void *x) { free(x); }

/**
 * @brief Duplicate a function pointer into heap storage.
 *
 * Stores the function pointer in a newly allocated slot so the generic map
 * can manage it. The caller should cast function pointer to/from void* when
 * calling map_insert/put.
 *
 * @param x Function pointer passed as void* (may be NULL).
 * @return void* Pointer to allocated storage containing the function pointer or NULL on OOM.
 */
static void *fp_dup(const void *x)
{
    if (!x)
        return NULL;
    fp_t f = (fp_t)(uintptr_t)x; /* caller passes function pointer cast via (void*)(uintptr_t)f */
    fp_t *d = malloc(sizeof(fp_t));
    if (!d)
        return NULL;
    *d = f;
    return d;
}

/**
 * @brief Free storage allocated by fp_dup.
 *
 * @param x Pointer returned by fp_dup.
 */
static void fp_free(void *x) { free(x); }

/* sample functions to store as values */

/**
 * @brief Example function to be stored as a value in the map.
 */
static void say_hello(void) { printf("hello\n"); }

/**
 * @brief Example function to be stored as a value in the map.
 */
static void say_goodbye(void) { printf("goodbye\n"); }

int main(void)
{
    /* map from uint32_t -> function pointer (both copied) */
    map_t *m = map_create(u32_cmp, u32_dup, u32_free, fp_dup, fp_free);
    if (!m)
        return 1;

    /* insert two entries */
    uint32_t k;
    k = 10;
    map_insert(m, &k, (void *)(uintptr_t)say_hello);
    k = 20;
    map_insert(m, &k, (void *)(uintptr_t)say_goodbye);

    printf("map size: %zu\n", map_size(m));

    /* lookup and call */
    uint32_t q = 10;
    fp_t *pv = map_find(m, &q);
    if (pv && *pv)
        (*pv)(); /* calls say_hello */

    /* iterate in order and call each function */
    printf("in-order traversal (key -> call value):\n");
    for (map_iter_t it = map_begin(m); it != map_end(m); it = map_next(it))
    {
        uint32_t *keyp = (uint32_t *)map_iter_key(it);
        fp_t *valp = (fp_t *)map_iter_value(it);
        printf("  %" PRIu32 " -> ", keyp ? *keyp : 0);
        if (valp && *valp)
            (*valp)();
        else
            printf("(null)\n");
    }

    /* erase an element */
    uint32_t rem = 20;
    map_erase(m, &rem);
    printf("after erase 20, size=%zu\n", map_size(m));

    /* clear and destroy */
    map_destroy(m);
    return 0;
}

/*
  Specialized, statically allocated map: uint32_t -> function pointer (fp_t).
  No malloc/free used. Fixed capacity pool.
*/

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

typedef void (*fp_t)(void);

#define MAX_NODES 256

typedef struct node
{
    uint32_t key;
    fp_t value;
    struct node *left, *right, *parent;
    int height;
    int in_use; /* 0 = free, 1 = used */
} node_t;

typedef struct
{
    node_t pool[MAX_NODES];
    size_t size;
    node_t *root;
} u32map_t;

/* helpers */

/**
 * @brief Return the height of a static node (0 if NULL).
 *
 * @param n Pointer to node_t or NULL.
 * @return int Height (>=0).
 */
static int node_height(node_t *n) { return n ? n->height : 0; }

/**
 * @brief Update height for static node based on its children.
 *
 * @param n Pointer to node_t (may be NULL).
 */
static void update_height(node_t *n)
{
    if (n)
    {
        int hl = node_height(n->left);
        int hr = node_height(n->right);
        n->height = (hl > hr ? hl : hr) + 1;
    }
}

/* pool management (no malloc/free) */

/**
 * @brief Initialize the static pool map, marking all slots free.
 *
 * @param m Pointer to u32map_t to initialize.
 */
static void pool_init(u32map_t *m)
{
    m->size = 0;
    m->root = NULL;
    for (int i = 0; i < MAX_NODES; ++i)
    {
        m->pool[i].in_use = 0;
        m->pool[i].left = m->pool[i].right = m->pool[i].parent = NULL;
        m->pool[i].height = 0;
        m->pool[i].key = 0;
        m->pool[i].value = NULL;
    }
}

/**
 * @brief Allocate a node from the static pool.
 *
 * Returns pointer to a free node initialized for use or NULL if pool exhausted.
 *
 * @param m Pointer to u32map_t.
 * @return node_t* Allocated node or NULL if none available.
 */
static node_t *node_alloc(u32map_t *m)
{
    for (int i = 0; i < MAX_NODES; ++i)
    {
        if (!m->pool[i].in_use)
        {
            node_t *n = &m->pool[i];
            n->in_use = 1;
            n->left = n->right = n->parent = NULL;
            n->height = 1;
            n->value = NULL;
            return n;
        }
    }
    return NULL; /* pool exhausted */
}

/**
 * @brief Return a node to the static pool (mark free).
 *
 * Does not call any user callbacks since values are plain function pointers.
 *
 * @param m Pointer to u32map_t (unused but kept for symmetry).
 * @param n Node to free.
 */
static void node_free(u32map_t *m, node_t *n)
{
    (void)m;
    if (!n)
        return;
    n->in_use = 0;
    n->left = n->right = n->parent = NULL;
    n->height = 0;
    n->value = NULL;
}

/* rotations */

/**
 * @brief Static-pool right rotation.
 *
 * @param y Root of subtree to rotate.
 * @return node_t* New root after rotation.
 */
static node_t *rotate_right(node_t *y)
{
    node_t *x = y->left;
    node_t *T2 = x->right;

    x->right = y;
    y->left = T2;

    if (T2)
        T2->parent = y;

    x->parent = y->parent;
    y->parent = x;

    update_height(y);
    update_height(x);
    return x;
}

/**
 * @brief Static-pool left rotation.
 *
 * @param x Root of subtree to rotate.
 * @return node_t* New root after rotation.
 */
static node_t *rotate_left(node_t *x)
{
    node_t *y = x->right;
    node_t *T2 = y->left;

    y->left = x;
    x->right = T2;

    if (T2)
        T2->parent = x;

    y->parent = x->parent;
    x->parent = y;

    update_height(x);
    update_height(y);
    return y;
}

/**
 * @brief Replace parent's child pointer with new_child for static pool map.
 *
 * @param m Pointer to u32map_t.
 * @param parent Parent node (may be NULL).
 * @param old_child Old child pointer to be replaced.
 * @param new_child New child pointer.
 */
static void set_child(u32map_t *m, node_t *parent, node_t *old_child, node_t *new_child)
{
    if (!parent)
    {
        m->root = new_child;
        if (new_child)
            new_child->parent = NULL;
    }
    else
    {
        if (parent->left == old_child)
            parent->left = new_child;
        else
            parent->right = new_child;
        if (new_child)
            new_child->parent = parent;
    }
}

/**
 * @brief Rebalance static pool subtree and return new subroot.
 *
 * @param m Pointer to u32map_t.
 * @param node Node to rebalance (may be NULL).
 * @return node_t* New root of subtree or NULL.
 */
static node_t *rebalance_at(u32map_t *m, node_t *node)
{
    if (!node)
        return NULL;
    update_height(node);
    int balance = node_height(node->left) - node_height(node->right);

    if (balance > 1)
    {
        if (node_height(node->left->left) >= node_height(node->left->right))
        {
            node_t *new_root = rotate_right(node);
            return new_root;
        }
        else
        {
            node->left = rotate_left(node->left);
            if (node->left)
                node->left->parent = node;
            node_t *new_root = rotate_right(node);
            return new_root;
        }
    }
    else if (balance < -1)
    {
        if (node_height(node->right->right) >= node_height(node->right->left))
        {
            node_t *new_root = rotate_left(node);
            return new_root;
        }
        else
        {
            node->right = rotate_right(node->right);
            if (node->right)
                node->right->parent = node;
            node_t *new_root = rotate_left(node);
            return new_root;
        }
    }
    return node;
}

/* API */

/**
 * @brief Initialize a statically allocated uint32_t->fp_t map.
 *
 * Must be called before using the map.
 *
 * @param m Pointer to u32map_t to initialize.
 */
static void map_init(u32map_t *m)
{
    pool_init(m);
}

/**
 * @brief Find a node by uint32_t key in the static map.
 *
 * @param m Pointer to u32map_t.
 * @param key Key to search for.
 * @return node_t* Node containing key or NULL if not found.
 */
static node_t *find_node(u32map_t *m, uint32_t key)
{
    node_t *cur = m->root;
    while (cur)
    {
        if (key == cur->key)
            return cur;
        cur = (key < cur->key) ? cur->left : cur->right;
    }
    return NULL;
}

/**
 * @brief Insert a key/value into the static map without overwriting.
 *
 * @param m Pointer to u32map_t.
 * @param key Key to insert.
 * @param value Function pointer value to store.
 * @return int 1 if inserted, 0 if existed, -1 if pool exhausted.
 */
static int map_insert(u32map_t *m, uint32_t key, fp_t value)
{
    if (!m->root)
    {
        node_t *n = node_alloc(m);
        if (!n)
            return -1;
        n->key = key;
        n->value = value;
        m->root = n;
        m->size = 1;
        return 1;
    }

    node_t *parent = NULL;
    node_t *cur = m->root;
    int cmpv = 0;
    while (cur)
    {
        if (key == cur->key)
            return 0;
        parent = cur;
        if (key < cur->key)
        {
            cur = cur->left;
            cmpv = -1;
        }
        else
        {
            cur = cur->right;
            cmpv = 1;
        }
    }

    node_t *n = node_alloc(m);
    if (!n)
        return -1;
    n->key = key;
    n->value = value;
    n->parent = parent;
    if (cmpv < 0)
        parent->left = n;
    else
        parent->right = n;
    m->size++;

    node_t *p = n->parent;
    while (p)
    {
        node_t *old_p = p;
        node_t *new_subroot = rebalance_at(m, p);
        set_child(m, new_subroot->parent, old_p, new_subroot);
        p = new_subroot->parent;
    }
    if (m->root && m->root->parent)
        m->root->parent = NULL;
    return 1;
}

/**
 * @brief Insert or replace a value in the static map.
 *
 * @param m Pointer to u32map_t.
 * @param key Key to insert or replace.
 * @param value Function pointer value.
 * @return int 1 if inserted, 2 if replaced, -1 if pool exhausted.
 */
static int map_put(u32map_t *m, uint32_t key, fp_t value)
{
    node_t *ex = find_node(m, key);
    if (ex)
    {
        ex->value = value;
        return 2;
    }
    return map_insert(m, key, value);
}

/**
 * @brief Find a function pointer value by key in the static map.
 *
 * @param m Pointer to u32map_t.
 * @param key Key to find.
 * @return fp_t Function pointer stored or NULL if not found.
 */
static fp_t map_find(u32map_t *m, uint32_t key)
{
    node_t *n = find_node(m, key);
    return n ? n->value : NULL;
}

/**
 * @brief Return leftmost node in a subtree (static pool).
 *
 * @param n Subtree root.
 * @return node_t* Minimum node or NULL.
 */
static node_t *subtree_min(node_t *n)
{
    if (!n)
        return NULL;
    while (n->left)
        n = n->left;
    return n;
}

/**
 * @brief Erase a node from the static map and return the parent for rebalancing.
 *
 * @param m Pointer to u32map_t.
 * @param n Node to erase.
 * @return node_t* Parent to continue rebalancing from.
 */
static node_t *erase_node(u32map_t *m, node_t *n)
{
    if (!n)
        return NULL;
    node_t *parent = NULL;

    if (n->left && n->right)
    {
        node_t *suc = subtree_min(n->right);
        uint32_t tkey = n->key;
        n->key = suc->key;
        suc->key = tkey;
        fp_t tval = n->value;
        n->value = suc->value;
        suc->value = tval;
        return erase_node(m, suc);
    }
    else
    {
        node_t *child = n->left ? n->left : n->right;
        parent = n->parent;
        if (parent == NULL)
        {
            m->root = child;
            if (child)
                child->parent = NULL;
        }
        else
        {
            if (parent->left == n)
                parent->left = child;
            else
                parent->right = child;
            if (child)
                child->parent = parent;
        }
        node_free(m, n);
        if (m->size > 0)
            m->size--;
        return parent;
    }
}

/**
 * @brief Erase a key from the static map.
 *
 * @param m Pointer to u32map_t.
 * @param key Key to erase.
 * @return int 1 if erased, 0 if not found.
 */
static int map_erase(u32map_t *m, uint32_t key)
{
    node_t *n = find_node(m, key);
    if (!n)
        return 0;
    node_t *p = erase_node(m, n);
    while (p)
    {
        node_t *old_p = p;
        node_t *new_subroot = rebalance_at(m, p);
        set_child(m, new_subroot->parent, old_p, new_subroot);
        p = new_subroot->parent;
    }
    if (m->root && m->root->parent)
        m->root->parent = NULL;
    return 1;
}

/**
 * @brief Return number of elements in static map.
 *
 * @param m Pointer to u32map_t.
 * @return size_t Number of stored elements.
 */
static size_t map_size(u32map_t *m) { return m ? m->size : 0; }

/**
 * @brief Return iterator to first element in static map.
 *
 * @param m Pointer to u32map_t.
 * @return node_t* First node or NULL.
 */
static node_t *map_begin(u32map_t *m) { return m ? subtree_min(m->root) : NULL; }

/**
 * @brief Return next node in-order for static map.
 *
 * @param it Current node pointer.
 * @return node_t* Next node or NULL.
 */
static node_t *map_next(node_t *it)
{
    if (!it)
        return NULL;
    if (it->right)
    {
        node_t *n = it->right;
        while (n->left)
            n = n->left;
        return n;
    }
    else
    {
        node_t *p = it->parent;
        node_t *cur = it;
        while (p && p->right == cur)
        {
            cur = p;
            p = p->parent;
        }
        return p;
    }
}

/* sample functions */

/**
 * @brief Example function for static map values: prints "hello".
 */
static void say_hello(void) { puts("hello"); }

/**
 * @brief Example function for static map values: prints "goodbye".
 */
static void say_goodbye(void) { puts("goodbye"); }

int main(void)
{
    u32map_t map;
    map_init(&map);

    /* insert two entries */
    if (map_insert(&map, 10, say_hello) < 0)
    {
        puts("pool full");
        return 1;
    }
    if (map_insert(&map, 20, say_goodbye) < 0)
    {
        puts("pool full");
        return 1;
    }

    printf("map size: %zu\n", map_size(&map));

    /* lookup and call */
    fp_t f = map_find(&map, 10);
    if (f)
        f();

    /* iterate in order and call each function */
    printf("in-order traversal (key -> call value):\n");
    for (node_t *it = map_begin(&map); it != NULL; it = map_next(it))
    {
        printf("  %" PRIu32 " -> ", it->key);
        if (it->value)
            it->value();
        else
            puts("(null)");
    }

    /* erase an element */
    map_erase(&map, 20);
    printf("after erase 20, size=%zu\n", map_size(&map));

    return 0;
}