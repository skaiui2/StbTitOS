#include "mg_alloc.h"
#include "membit.h"
#include "heap.h"
#include "lmath.h"

#include <stdint.h>
#include <stdio.h>

#define MG_MAX_CLASS_BITS        12
#define MG_MIN_CLASS_BYTES       32

#define MG_POOL_BLOCKS_SMALL     8
#define MG_POOL_BLOCKS_MEDIUM    2
#define MG_POOL_BLOCKS_LARGE     1

#define MG_BLOCK_THRESHOLD_SMALL 64
#define MG_BLOCK_THRESHOLD_MED   128

struct mg_pool_node {
    membit_pool_t pool;
    uint16_t      block_count;
    uint16_t      block_size;
    struct mg_pool_node *next;
};

struct mg_class {
    struct mg_pool_node *pools;
    size_t               block_size;
};

struct mg_block_header {
    membit_pool_t pool;
};

enum mg_region_kind {
    MG_REGION_POOL = 0,
    MG_REGION_BUMP = 1,
};

struct mg_bump_state {
    uint8_t *buf;
    size_t   cap;
    size_t   off;
};

struct mg_region {
    enum mg_region_kind kind;
    uint8_t             max_bits;

    union {
        struct {
            struct mg_class classes[MG_MAX_CLASS_BITS + 1];
        } pool;

        struct mg_bump_state bump;
    } u;
};

static inline uint8_t mg_pool_block_count(size_t block_size)
{
    if (block_size <= MG_BLOCK_THRESHOLD_SMALL)
        return MG_POOL_BLOCKS_SMALL;
    if (block_size <= MG_BLOCK_THRESHOLD_MED)
        return MG_POOL_BLOCKS_MEDIUM;
    return MG_POOL_BLOCKS_LARGE;
}

static struct mg_pool_node *mg_pool_node_new(size_t block_size)
{
    uint8_t count = mg_pool_block_count(block_size);
    membit_pool_t p = membit_create((uint16_t)block_size, count);
    if (!p)
        return NULL;

    struct mg_pool_node *n = heap_malloc(sizeof(*n));
    if (!n) {
        membit_destroy(p);
        return NULL;
    }

    n->pool        = p;
    n->block_count = count;
    n->block_size  = (uint16_t)block_size;
    n->next        = NULL;
    return n;
}

mg_region_handle mg_region_create_pool(uint8_t max_class_bits)
{
    struct mg_region *r = heap_malloc(sizeof(struct mg_region));
    if (!r)
        return NULL;

    if (max_class_bits > MG_MAX_CLASS_BITS)
        max_class_bits = MG_MAX_CLASS_BITS;

    r->kind     = MG_REGION_POOL;
    r->max_bits = max_class_bits;

    for (uint8_t i = 0; i <= r->max_bits; i++) {
        r->u.pool.classes[i].pools      = NULL;
        r->u.pool.classes[i].block_size = (size_t)1u << i;
    }

    return r;
}

mg_region_handle mg_region_create_bump(size_t cap)
{
    struct mg_region *r = heap_malloc(sizeof(struct mg_region));
    if (!r)
        return NULL;

    uint8_t *buf = heap_malloc(cap);
    if (!buf) {
        heap_free(r);
        return NULL;
    }

    r->kind     = MG_REGION_BUMP;
    r->max_bits = 0;

    r->u.bump.buf = buf;
    r->u.bump.cap = cap;
    r->u.bump.off = 0;

    return r;
}

static void *mg_region_alloc_class(struct mg_region *r, size_t size)
{
    size_t  need = size + sizeof(struct mg_block_header);
    uint8_t bits = next_power_of_two_index_u32((uint32_t)need);

    if (bits > r->max_bits)
        return NULL;

    for (uint8_t b = bits; b <= r->max_bits; b++) {
        struct mg_class *c = &r->u.pool.classes[b];

        for (struct mg_pool_node *n = c->pools; n; n = n->next) {
            void *blk = membit_alloc(n->pool);
            if (blk) {
                struct mg_block_header *h = (struct mg_block_header *)blk;
                h->pool = n->pool;
                return (uint8_t *)blk + sizeof(struct mg_block_header);
            }
        }
    }

    struct mg_class *c = &r->u.pool.classes[bits];

    struct mg_pool_node *n = mg_pool_node_new(c->block_size);
    if (!n)
        return NULL;

    n->next  = c->pools;
    c->pools = n;

    void *blk = membit_alloc(n->pool);
    if (!blk)
        return NULL;

    struct mg_block_header *h = (struct mg_block_header *)blk;
    h->pool = n->pool;
    return (uint8_t *)blk + sizeof(struct mg_block_header);
}

static void *mg_region_alloc_bump(struct mg_region *r, size_t size)
{
    struct mg_bump_state *b = &r->u.bump;

    size = (size + 3) & ~((size_t)3);

    if (b->off + size > b->cap)
        return NULL;

    void *p = b->buf + b->off;
    b->off += size;
    return p;
}

void *mg_region_alloc(mg_region_handle r, size_t size)
{
    if (!r || !size)
        return NULL;

    if (r->kind == MG_REGION_BUMP) {
        return mg_region_alloc_bump(r, size);
    }

    if (size < MG_MIN_CLASS_BYTES)
        size = MG_MIN_CLASS_BYTES;

    return mg_region_alloc_class(r, size);
}

void mg_region_reset(mg_region_handle r)
{
    if (!r)
        return;

    if (r->kind == MG_REGION_BUMP) {
        r->u.bump.off = 0;
        return;
    }

    for (uint8_t i = 0; i <= r->max_bits; i++) {
        for (struct mg_pool_node *n = r->u.pool.classes[i].pools; n; n = n->next) {
            membit_reset(n->pool);
        }
    }
}

void mg_region_destroy(mg_region_handle r)
{
    if (!r)
        return;

    if (r->kind == MG_REGION_BUMP) {
        if (r->u.bump.buf)
            heap_free(r->u.bump.buf);
        heap_free(r);
        return;
    }

    for (uint8_t i = 0; i <= r->max_bits; i++) {
        struct mg_pool_node *n = r->u.pool.classes[i].pools;
        while (n) {
            struct mg_pool_node *next = n->next;
            membit_destroy(n->pool);
            heap_free(n);
            n = next;
        }
    }

    heap_free(r);
}

void mg_region_print_pools(mg_region_handle r)
{
    if (!r) return;

    printf("=== MG Region Pools (Full Memory Stats) ===\n");

    if (r->kind == MG_REGION_BUMP) {
        printf("Bump region: cap=%u, used=%u bytes\n",
               (unsigned)r->u.bump.cap, (unsigned)r->u.bump.off);
        printf("==========================================\n");
        return;
    }

    unsigned total_all_classes = 0;

    for (uint8_t i = 0; i <= r->max_bits; i++) {
        struct mg_class *c = &r->u.pool.classes[i];
        unsigned pool_count = 0;
        unsigned total_pool_mem = 0;

        for (struct mg_pool_node *n = c->pools; n; n = n->next) {
            unsigned data_mem   = n->block_count * n->block_size;
            unsigned header_mem = n->block_count * sizeof(struct mg_block_header);
            unsigned membit_mem = (unsigned)((sizeof(struct membit_pool) + 0x07) & ~0x07)
                                  + data_mem;
            unsigned node_mem   = sizeof(struct mg_pool_node);

            unsigned pool_mem = node_mem + membit_mem + header_mem;

            printf("Class %u (block_size=%u): Pool %u -> blocks=%u, block_size=%u, "
                   "data=%u, header=%u, membit=%u, node=%u, total=%u bytes\n",
                   (unsigned)i, (unsigned)c->block_size, pool_count,
                   (unsigned)n->block_count, (unsigned)n->block_size,
                   data_mem, header_mem, membit_mem, node_mem, pool_mem);

            total_pool_mem += pool_mem;
            pool_count++;
        }

        if (pool_count > 0) {
            printf("Class %u total pools=%u, total_pool_mem=%u bytes\n\n",
                   (unsigned)i, pool_count, total_pool_mem);
            total_all_classes += total_pool_mem;
        }
    }

    printf("MG Region total memory (approx) = %u bytes\n", total_all_classes);
    printf("==========================================\n");
}
