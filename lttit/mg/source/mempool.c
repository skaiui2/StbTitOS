#include "mempool.h"
#include "link_list.h"
#include "heap.h"
#include "macro.h"
#include <stddef.h>

#define ALIGNMENT_BYTE 0x07

struct pool_node {
    uint8_t used;
    struct list_node free_node;
};

struct pool_head {
    struct pool_node *head;
    struct list_node free_list;
    size_t block_size;
    size_t all_count;
    uint16_t remain_node;
};

static const size_t node_struct_size =
    (sizeof(struct pool_node) + (size_t)ALIGNMENT_BYTE) & ~(ALIGNMENT_BYTE);

static const size_t head_struct_size =
    (sizeof(struct pool_head) + (size_t)ALIGNMENT_BYTE) & ~(ALIGNMENT_BYTE);

static void pool_apart(struct pool_head *pool, uint16_t amount, size_t apart_size)
{
    struct pool_node *prev;
    struct pool_node *new_node;

    prev = pool->head;

    while (amount) {
        new_node = (struct pool_node *)(((size_t)prev) + apart_size);
        new_node->used = 0;
        list_add_next(&prev->free_node, &new_node->free_node);
        prev = new_node;
        amount--;
    }
}

pool_head_handle mem_pool_create(uint16_t size, uint16_t amount)
{
    size_t align_req;
    size_t apart_size;
    uint32_t total_size;
    void *start;
    struct pool_head *pool;
    struct pool_node *first;

    if (amount == 0 || size == 0) {
        return NULL;
    }

    apart_size = size;
    apart_size += node_struct_size;

    if (apart_size & ALIGNMENT_BYTE) {
        align_req = (size_t)ALIGNMENT_BYTE + 1 - (apart_size & ALIGNMENT_BYTE);
        apart_size += align_req;
    }

    total_size = (uint32_t)amount * (uint32_t)apart_size;
    total_size += head_struct_size;

    start = heap_malloc(total_size);
    if (start == NULL) {
        return NULL;
    }

    pool = (struct pool_head *)start;
    pool->head = (struct pool_node *)((size_t)start + head_struct_size);
    pool->block_size = size;
    pool->all_count = amount;
    pool->remain_node = amount;

    pool->head->used = 0;

    list_node_init(&pool->free_list);

    first = pool->head;
    list_add_next(&pool->free_list, &first->free_node);

    if (amount > 1) {
        pool_apart(pool, (uint16_t)(amount - 1), apart_size);
    }

    return pool;
}

void *mem_pool_alloc(pool_head_handle pool)
{
    struct pool_node *node;
    void *ret;

    ret = NULL;

    if (pool == NULL) {
        return NULL;
    }

    if (pool->free_list.next == &pool->free_list) {
        return NULL;
    }

    node = container_of(pool->free_list.next, struct pool_node, free_node);
    list_remove(pool->free_list.next);

    if (node->used) {
        return NULL;
    }

    node->used = 1;
    if (pool->remain_node > 0) {
        pool->remain_node--;
    }
    ret = (void *)(((uint8_t *)node) + node_struct_size);

    return ret;
}

void mem_pool_free(pool_head_handle pool, void *ptr)
{
    struct pool_node *blk;
    struct list_node *pos;
    struct pool_node *n;

    if (pool == NULL || ptr == NULL) {
        return;
    }

    blk = (struct pool_node *)((size_t)ptr - node_struct_size);

    if (!blk->used) {
        return;
    }

    blk->used = 0;

    pos = pool->free_list.next;
    while (pos != &pool->free_list) {
        n = container_of(pos, struct pool_node, free_node);
        if (n > blk) {
            break;
        }
        pos = pos->next;
    }

    list_add_prev(pos, &blk->free_node);

    pool->remain_node++;
}

void mem_pool_delete(pool_head_handle pool)
{
    if (pool != NULL) {
        heap_free(pool);
    }
}

uint16_t mem_pool_free_nodes(pool_head_handle pool)
{
    if (pool == NULL) {
        return 0;
    }

    return pool->remain_node;
}
