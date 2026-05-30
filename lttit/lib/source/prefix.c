#include "prefix.h"
#include "heap.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int is_inner(void *p) {
    return ((uintptr_t)p & 1u) != 0;
}

static struct prefix_node *as_inner(void *p) {
    return (struct prefix_node *)((char *)p - 1);
}

static struct prefix_leaf *as_leaf(void *p) {
    return (struct prefix_leaf *)p;
}

static void *mark_inner(struct prefix_node *n) {
    return (void *)((uintptr_t)n + 1u);
}

void prefix_map_init(struct prefix_map *m) {
    m->root = 0;
}

static unsigned char key_byte(const char *s, size_t len, unsigned int byte) {
    if (byte < len) return (unsigned char)s[byte];
    return 0;
}

static struct prefix_leaf *leaf_new(const char *key, void *value) {
    size_t len = strlen(key);
    struct prefix_leaf *l = heap_malloc(sizeof(*l));
    if (!l) return 0;
    l->key = heap_malloc(len + 1);
    if (!l->key) {
        heap_free(l);
        return 0;
    }
    memcpy(l->key, key, len + 1);
    l->value = value;
    return l;
}

static void leaf_free(struct prefix_leaf *l) {
    heap_free(l->key);
    heap_free(l);
}

void *prefix_map_get(struct prefix_map *m, const char *key) {
    const char *u = key;
    size_t ulen = strlen(u);
    void *p = m->root;

    while (p && is_inner(p)) {
        struct prefix_node *n = as_inner(p);
        unsigned char c = key_byte(u, ulen, n->byte);
        int d = (1 + (n->otherbits | c)) >> 8;
        p = n->child[d];
    }

    if (!p) return 0;
    struct prefix_leaf *l = as_leaf(p);
    if (strcmp(l->key, u) == 0) return l->value;
    return 0;
}

static int first_diff(const char *a, size_t la,
                      const char *b, size_t lb,
                      unsigned int *byte_out,
                      unsigned char *mask_out)
{
    unsigned int i = 0;
    unsigned int m = la < lb ? la : lb;

    for (; i < m; i++) {
        unsigned char x = (unsigned char)a[i] ^ (unsigned char)b[i];
        if (x) {
            unsigned char bit = 0x80;
            while (!(x & bit)) bit >>= 1;
            *byte_out = i;
            *mask_out = bit;
            return 0;
        }
    }

    if (la != lb) {
        unsigned char x = (unsigned char)((la == m) ? b[m] : a[m]);
        unsigned char bit = 0x80;
        while (!(x & bit)) bit >>= 1;
        *byte_out = m;
        *mask_out = bit;
        return 0;
    }

    return -1;
}

int prefix_map_set(struct prefix_map *m, const char *key, void *value) {
    const char *u = key;
    size_t ulen = strlen(u);
    void *p = m->root;

    if (!p) {
        struct prefix_leaf *l = leaf_new(u, value);
        if (!l) return -1;
        m->root = l;
        return 0;
    }

    void **where = &m->root;
    while (is_inner(p)) {
        struct prefix_node *n = as_inner(p);
        unsigned char c = key_byte(u, ulen, n->byte);
        int d = (1 + (n->otherbits | c)) >> 8;
        where = &n->child[d];
        p = *where;
    }

    struct prefix_leaf *old = as_leaf(p);
    const char *k = old->key;
    size_t klen = strlen(k);

    if (strcmp(k, u) == 0) {
        old->value = value;
        return 0;
    }

    unsigned int byte;
    unsigned char mask;
    if (first_diff(k, klen, u, ulen, &byte, &mask) < 0) {
        old->value = value;
        return 0;
    }

    mask |= mask >> 1;
    mask |= mask >> 2;
    mask |= mask >> 4;
    mask = (mask & ~(mask >> 1)) ^ 255;

    unsigned char c_new = key_byte(u, ulen, byte);
    int d_new = (1 + (mask | c_new)) >> 8;

    struct prefix_node *node = heap_malloc(sizeof(*node));
    if (!node) return -1;

    struct prefix_leaf *l = leaf_new(u, value);
    if (!l) {
        heap_free(node);
        return -1;
    }

    node->byte = byte;
    node->otherbits = mask;

    void **ins = &m->root;
    void *cur = m->root;

    while (is_inner(cur)) {
        struct prefix_node *q = as_inner(cur);
        if (q->byte > byte) break;
        if (q->byte == byte && q->otherbits > mask) break;

        unsigned char c = key_byte(u, ulen, q->byte);
        int d = (1 + (q->otherbits | c)) >> 8;
        ins = &q->child[d];
        cur = *ins;
    }

    node->child[d_new] = l;
    node->child[1 - d_new] = cur;
    *ins = mark_inner(node);

    return 0;
}

int prefix_map_del(struct prefix_map *m, const char *key) {
    const char *u = key;
    size_t ulen = strlen(u);
    void *p = m->root;
    if (!p) return 0;

    void **where = &m->root;
    void **whereq = 0;
    struct prefix_node *q = 0;
    int d = 0;

    while (is_inner(p)) {
        whereq = where;
        q = as_inner(p);
        unsigned char c = key_byte(u, ulen, q->byte);
        d = (1 + (q->otherbits | c)) >> 8;
        where = &q->child[d];
        p = *where;
        if (!p) return 0;
    }

    struct prefix_leaf *l = as_leaf(p);
    if (strcmp(l->key, u) != 0) return 0;

    leaf_free(l);

    if (!whereq) {
        m->root = 0;
        return 1;
    }

    *whereq = q->child[1 - d];
    heap_free(q);
    return 1;
}

static int iter_rec(void *p,
                    int (*fn)(const char *key, void *value, void *arg),
                    void *arg)
{
    if (!p) return 1;
    if (is_inner(p)) {
        struct prefix_node *n = as_inner(p);
        if (!iter_rec(n->child[0], fn, arg)) return 0;
        if (!iter_rec(n->child[1], fn, arg)) return 0;
        return 1;
    } else {
        struct prefix_leaf *l = as_leaf(p);
        return fn(l->key, l->value, arg) == 0;
    }
}

int prefix_map_iter(struct prefix_map *m,
                    int (*fn)(const char *key, void *value, void *arg),
                    void *arg)
{
    if (!m->root) return 1;
    return iter_rec(m->root, fn, arg);
}

static int iter_prefix_rec(void *p,
                           int (*fn)(const char *key, void *value, void *arg),
                           void *arg)
{
    if (!p) return 1;
    if (is_inner(p)) {
        struct prefix_node *n = as_inner(p);
        if (!iter_prefix_rec(n->child[0], fn, arg)) return 0;
        if (!iter_prefix_rec(n->child[1], fn, arg)) return 0;
        return 1;
    } else {
        struct prefix_leaf *l = as_leaf(p);
        return fn(l->key, l->value, arg) == 0;
    }
}

int prefix_map_iter_prefix(struct prefix_map *m, const char *prefix,
                           int (*fn)(const char *key, void *value, void *arg),
                           void *arg)
{
    const char *u = prefix;
    size_t ulen = strlen(u);
    void *p = m->root;
    void *top = p;

    if (!p) return 1;

    while (is_inner(p)) {
        struct prefix_node *n = as_inner(p);
        unsigned char c = key_byte(u, ulen, n->byte);
        int d = (1 + (n->otherbits | c)) >> 8;
        p = n->child[d];
        if (!p) return 1;
        if (n->byte < ulen)
            top = p;
    }

    struct prefix_leaf *l = as_leaf(p);
    if (strncmp(l->key, u, ulen) != 0)
        return 1;

    return iter_prefix_rec(top, fn, arg);
}

static void clear_rec(void *p) {
    if (!p) return;
    if (is_inner(p)) {
        struct prefix_node *n = as_inner(p);
        clear_rec(n->child[0]);
        clear_rec(n->child[1]);
        heap_free(n);
    } else {
        leaf_free(as_leaf(p));
    }
}

void prefix_map_clear(struct prefix_map *m) {
    clear_rec(m->root);
    m->root = 0;
}

