#include "heap.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define PTR_SIZE        uint32_t
#define CONFIG_HEAP     (60 * 1024)
#define ALIGNMENT_BYTE  0x07

#define MIN_SIZE ((size_t)(heap_struct_size << 1))

struct heap_node {
    struct heap_node *next;
    size_t block_size;
};

struct heap_head {
    struct heap_node head;
    struct heap_node *tail;
    size_t all_size;
};

static struct heap_head the_heap = {
        .tail     = NULL,
        .all_size = CONFIG_HEAP,
};

static uint8_t all_heap[CONFIG_HEAP];

static const size_t heap_struct_size =
        (sizeof(struct heap_node) + (size_t)ALIGNMENT_BYTE) & ~(ALIGNMENT_BYTE);

static void insert_free_block(struct heap_node *node);

#if HEAP_TRACKING

#define HEAP_TRACK_MAX 128

struct heap_track_entry {
    void        *ptr;
    size_t       size;
    const char  *file;
    int          line;
};

static struct heap_track_entry g_heap_track[HEAP_TRACK_MAX];

static void heap_track_alloc(void *ptr, size_t size,
                             const char *file, int line)
{
    if (!ptr)
        return;

    for (int i = 0; i < HEAP_TRACK_MAX; i++) {
        if (g_heap_track[i].ptr == NULL) {
            g_heap_track[i].ptr  = ptr;
            g_heap_track[i].size = size;
            g_heap_track[i].file = file;
            g_heap_track[i].line = line;
            return;
        }
    }
}

static void heap_track_free(void *ptr)
{
    if (!ptr)
        return;

    for (int i = 0; i < HEAP_TRACK_MAX; i++) {
        if (g_heap_track[i].ptr == ptr) {
            g_heap_track[i].ptr  = NULL;
            g_heap_track[i].size = 0;
            g_heap_track[i].file = NULL;
            g_heap_track[i].line = 0;
            return;
        }
    }
}

void heap_debug_dump_leaks(void)
{
    printf("=== heap outstanding allocations ===\n");
    for (int i = 0; i < HEAP_TRACK_MAX; i++) {
        if (g_heap_track[i].ptr) {
            printf("  leak: ptr=%p size=%u at %s:%d\n",
                   g_heap_track[i].ptr,
                   (unsigned)g_heap_track[i].size,
                   g_heap_track[i].file ? g_heap_track[i].file : "?",
                   g_heap_track[i].line);
        }
    }
    printf("====================================\n");
}

#else

#define heap_track_alloc(ptr,size,file,line)
#define heap_track_free(ptr)
void heap_debug_dump_leaks(void) {}

#endif

void heap_init(void)
{
    struct heap_node *first;
    PTR_SIZE start, end;

    start = (PTR_SIZE)all_heap;

    if (start & ALIGNMENT_BYTE) {
        start += ALIGNMENT_BYTE;
        start &= ~ALIGNMENT_BYTE;
        the_heap.all_size -= (size_t)(start - (PTR_SIZE)all_heap);
    }

    the_heap.head.next       = (struct heap_node *)start;
    the_heap.head.block_size = 0;

    end = start + (uint32_t)the_heap.all_size - (uint32_t)heap_struct_size;

    if (end & ALIGNMENT_BYTE) {
        end &= ~ALIGNMENT_BYTE;
        the_heap.all_size = (size_t)(end - start);
    }

    the_heap.tail             = (struct heap_node *)end;
    the_heap.tail->block_size = 0;
    the_heap.tail->next       = NULL;

    first             = (struct heap_node *)start;
    first->next       = the_heap.tail;
    first->block_size = the_heap.all_size;
}

static void *real_heap_malloc(size_t size)
{
    struct heap_node *prev, *use, *new;
    size_t align_req;
    void *ret = NULL;

    size += heap_struct_size;

    if (size & ALIGNMENT_BYTE) {
        align_req = (ALIGNMENT_BYTE + 1) - (size & ALIGNMENT_BYTE);
        size += align_req;
    }

    if (the_heap.tail == NULL)
        heap_init();

    prev = &the_heap.head;
    use  = the_heap.head.next;

    while (use->block_size < size) {
        prev = use;
        use  = use->next;
        if (!use)
            return NULL;
    }

    ret = (void *)(((uint8_t *)use) + heap_struct_size);

    prev->next = use->next;

    if ((use->block_size - size) > MIN_SIZE) {
        new             = (struct heap_node *)(((uint8_t *)use) + size);
        new->block_size = use->block_size - size;
        use->block_size = size;
        new->next       = prev->next;
        prev->next      = new;
    }

    the_heap.all_size -= use->block_size;
    use->next          = NULL;

    return ret;
}

static void real_heap_free(void *ptr)
{
    if (!ptr)
        return;

    uint8_t *raw = (uint8_t *)ptr - heap_struct_size;
    struct heap_node *node = (struct heap_node *)raw;

    the_heap.all_size += node->block_size;

    insert_free_block(node);
}

static void insert_free_block(struct heap_node *node)
{
    struct heap_node *iter;
    uint8_t *addr;

    for (iter = &the_heap.head; iter->next < node; iter = iter->next)
        ;

    node->next = iter->next;
    iter->next = node;

    addr = (uint8_t *)node;

    if ((addr + node->block_size) == (uint8_t *)node->next) {
        if (node->next != the_heap.tail) {
            node->block_size += node->next->block_size;
            node->next        = node->next->next;
        } else {
            node->next = the_heap.tail;
        }
    }

    addr = (uint8_t *)iter;

    if ((addr + iter->block_size) == (uint8_t *)node) {
        iter->block_size += node->block_size;
        iter->next        = node->next;
    }
}

struct heap_stats heap_get_stats(void)
{
    struct heap_stats st = {0};

    st.remain_size = the_heap.all_size;

    struct heap_node *node = the_heap.head.next;

    while (node) {
        st.free_size_iter += node->block_size;

        if (node->block_size > st.max_free_block)
            st.max_free_block = node->block_size;

        st.free_blocks++;

        node = node->next;
    }

    return st;
}

void *heap_malloc_dbg(size_t size, const char *file, int line)
{
    void *ret = real_heap_malloc(size);
    heap_track_alloc(ret, size, file, line);
    return ret;
}

void heap_free_dbg(void *ptr, const char *file, int line)
{
    heap_track_free(ptr);
    real_heap_free(ptr);
}
