#ifndef _MEMPOOL_H_
#define _MEMPOOL_H_

#include <stdint.h>

struct pool_head;
typedef struct pool_head *pool_head_handle;

pool_head_handle mem_pool_create(uint16_t size, uint16_t amount);
void *mem_pool_alloc(pool_head_handle pool);
void mem_pool_free(pool_head_handle pool, void *ptr);
void mem_pool_delete(pool_head_handle pool);

#endif
