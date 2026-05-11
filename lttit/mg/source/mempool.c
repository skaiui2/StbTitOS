#include "mempool.h"
#include "link_list.h"
#include "heap.h"
#include "macro.h"
#include <stddef.h>

#define ALIGNMENT_BYTE 0x07

struct pool_node {
    uint8_t used;
    struct list_node free_node;
    struct pool_node *next;
};

struct pool_head {
    struct pool_node *head;
    struct list_node free_list;
    size_t block_size;
    size_t all_count;
    uint8_t remain_node;
};

static const size_t node_struct_size =
        (sizeof(struct pool_node) + (size_t)ALIGNMENT_BYTE) & ~(ALIGNMENT_BYTE);

static const size_t head_struct_size =
        (sizeof(struct pool_head) + (size_t)ALIGNMENT_BYTE) & ~(ALIGNMENT_BYTE);

static void pool_apart(struct pool_head *pool, uint16_t amount, size_t apart_size)
{
    struct pool_node *prev, *new;

    prev = pool->head;

    while (amount) {
        new = (struct pool_node *)(((size_t)prev) + apart_size);
        new->used = 0;
        list_add_next(&prev->free_node, &new->free_node);
        prev->next = new;
        prev = new;
        amount--;
    }

    new->next = NULL;
}

pool_head_handle mem_pool_create(uint16_t size, uint16_t amount)
{
    size_t align_req;
    size_t apart_size = size;
    uint32_t total_size;
    void *start;
    struct pool_head *pool;
    struct pool_node *first;

    apart_size += node_struct_size;

    if (apart_size & ALIGNMENT_BYTE) {
        align_req = ALIGNMENT_BYTE + 1 - (apart_size & ALIGNMENT_BYTE);
        apart_size += align_req;
    }

    total_size = (uint32_t)amount * (uint32_t)apart_size;
    total_size += head_struct_size;

    start = heap_malloc(total_size);
    if (!start)
        return NULL;

    pool = (struct pool_head *)start;

    *pool = (struct pool_head){
            .head = (struct pool_node *)((size_t)start + head_struct_size),
            .block_size = size,
            .all_count = amount,
            .remain_node = amount
    };

    pool->head->used = 0;

    list_node_init(&pool->free_list);

    first = pool->head;
    list_add_next(&pool->free_list, &first->free_node);

    pool_apart(pool, amount - 1, apart_size);

    return pool;
}

void *mem_pool_alloc(pool_head_handle pool)
{
    struct pool_node *node;
    void *ret = NULL;

    if (!pool)
        return NULL;

    if (pool->free_list.next != &pool->free_list) {
        node = container_of(pool->free_list.next, struct pool_node, free_node);
        list_remove(pool->free_list.next);
    } else {
        return NULL;
    }

    if (!node->used) {
        node->used = 1;
        pool->remain_node--;
        ret = (void *)(((uint8_t *)node) + node_struct_size);
    }

    return ret;
}

void mem_pool_free(pool_head_handle pool, void *ptr)
{
    struct pool_node *blk, *n;
    void *raw;

    if (!ptr)
        return;

    raw = (void *)((size_t)ptr - node_struct_size);
    blk = (struct pool_node *)raw;

    blk->used = 0;

    n = blk->next;

    while (n && n->used)
        n = n->next;

    if (n)
        list_add_prev(&n->free_node, &blk->free_node);
    else
        list_add_prev(&pool->free_list, &blk->free_node);
    pool->remain_node++;
}

void mem_pool_delete(pool_head_handle pool)
{
    if (pool)
        heap_free(pool);
}
