#include "bpf_builder.h"
#include "mg_alloc.h"
#include "lexer.h"
#include "inter.h"
#include "heap.h"
#include <string.h>
#include <stdint.h>

mg_region_handle backend_region;

void bpf_builder_init(struct bpf_builder *b, uint32_t cap)
{
    backend_region = mg_region_create_bump(cap);

    b->count    = 0;
    b->capacity = 128;

    b->insns = mg_region_alloc(backend_region,
                               sizeof(struct bpf_insn) * b->capacity);
    if (!b->insns) {
        while (1) { }
    }
}

void bpf_builder_free(struct bpf_builder *b)
{
    b->insns    = NULL;
    b->count    = 0;
    b->capacity = 0;

    mg_region_destroy(string_region);
    mg_region_destroy(backend_region);
}

void bpf_builder_reset(struct bpf_builder *b)
{
    b->count = 0;
}

static void bpf_builder_grow(struct bpf_builder *b)
{
    int new_cap = b->capacity * 2;

    struct bpf_insn *new_insns =
            mg_region_alloc(backend_region,
                            sizeof(struct bpf_insn) * new_cap);
    if (!new_insns) {
        while (1) { }
    }

    memcpy(new_insns, b->insns,
           (size_t)b->count * sizeof(struct bpf_insn));

    b->insns    = new_insns;
    b->capacity = new_cap;
}

int bpf_builder_emit(struct bpf_builder *b, struct bpf_insn insn)
{
    if (b->count >= b->capacity)
        bpf_builder_grow(b);

    b->insns[b->count] = insn;
    return b->count++;
}

struct bpf_insn *bpf_builder_data(struct bpf_builder *b)
{
    return b->insns;
}

int bpf_builder_count(struct bpf_builder *b)
{
    return b->count;
}

uint8_t *ccbpf_pack_memory(struct bpf_insn *insns, 
                           size_t insn_count,
                           size_t *out_len)
{
    struct CCBPF_Header hdr = (struct CCBPF_Header){0};

    hdr.magic   = CCBPF_MAGIC;
    hdr.version = 1;
    hdr.flags   = 0;

    hdr.code_offset = (uint32_t)sizeof(struct CCBPF_Header);
    hdr.code_size   = (uint32_t)(insn_count * sizeof(struct bpf_insn));

    uint32_t str_size = sizeof(int);
    for (int i = 0; i < string_pool_count; i++) {
        int len = (int)strlen(string_pool[i]) + 1;
        str_size += sizeof(int);
        str_size += (uint32_t)len;
    }

    hdr.data_offset = hdr.code_offset + hdr.code_size;
    hdr.data_size   = str_size;
    hdr.entry       = 0;

    size_t total_len = sizeof(struct CCBPF_Header)
                       + hdr.code_size
                       + hdr.data_size;

    uint8_t *buf = heap_malloc(total_len);
    if (!buf)
        return NULL;

    uint8_t *p = buf;

    memcpy(p, &hdr, sizeof(hdr));
    p += sizeof(hdr);

    memcpy(p, insns, insn_count * sizeof(struct bpf_insn));
    p += insn_count * sizeof(struct bpf_insn);

    memcpy(p, &string_pool_count, sizeof(int));
    p += sizeof(int);

    for (int i = 0; i < string_pool_count; i++) {
        int len = (int)strlen(string_pool[i]) + 1;

        memcpy(p, &len, sizeof(int));
        p += sizeof(int);

        memcpy(p, string_pool[i], (size_t)len);
        p += len;
    }

    if (out_len)
        *out_len = total_len;

    return buf;
}
