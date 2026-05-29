#include <stdint.h>
#include <string.h>
#include "ccbpf.h"
#include "cbpf.h"
#include "bpf_format.h"
#include "heap.h"
#include "hashmap.h"

struct hook_node {
    struct ccbpf_program *prog;
    struct hook_node *next;
};

struct hook_entry {
    const char *name;
    struct hook_node *head;
};

struct hashmap hook_table;

struct hashmap native_table;

static char *heap_strdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *p = heap_malloc(n);
    memcpy(p, s, n);
    return p;
}

void hook_register(const char *name)
{
    struct hook_entry *e = heap_malloc(sizeof(*e));
    e->name = heap_strdup(name);
    e->head = NULL;

    hashmap_put(&hook_table, (void*)e->name, e);
}

static struct hook_entry *find_hook(const char *name)
{
    return hashmap_get(&hook_table, (void*)name);
}

int hook_attach(const char *hook_name, uint8_t *image, size_t len)
{
    struct hook_entry *h = find_hook(hook_name);
    if (!h)
        return -1;

    struct ccbpf_program *prog = ccbpf_load_from_memory(image, len);
    if (!prog)
        return -1;

    struct hook_node *node = heap_malloc(sizeof(*node));
    if (!node) {
        ccbpf_unload(prog);
        return -1;
    }

    node->prog = prog;
    node->next = h->head;
    h->head = node;

    printf("[hook] ATTACH %s (prog=%p)\n", hook_name, prog);
    return 0;
}

int hook_detach(const char *hook_name)
{
    struct hook_entry *h = find_hook(hook_name);
    if (!h)
        return -1;

    struct hook_node *cur = h->head;
    while (cur) {
        struct hook_node *next = cur->next;
        ccbpf_unload(cur->prog);
        heap_free(cur);
        cur = next;
    }

    h->head = NULL;
    printf("[hook] DETACH %s\n", hook_name);
    return 0;
}

uint32_t hook_run(const char *hook_name, uint8_t *frame, size_t frame_size)
{
    struct hook_entry *h = find_hook(hook_name);
    if (!h)
        return 0;

    uint32_t last_ret = 0;

    for (struct hook_node *n = h->head; n; n = n->next) {
        n->prog->ctx = frame; 
        last_ret = ccbpf_run_frame(n->prog, frame, frame_size);
    }

    return last_ret;
}


void native_register(int func_id, int argc, native_fn_t fn)
{
    struct native_entry *e = heap_malloc(sizeof(*e));
    e->func_id = func_id;
    e->argc    = argc;
    e->fn      = fn;

    hashmap_put(&native_table, (void*)(uintptr_t)func_id, e);
}

struct ccbpf_program *ccbpf_load_from_memory(const uint8_t *image, size_t len)
{
    if (!image || len < sizeof(struct CCBPF_Header))
        return NULL;

    const struct CCBPF_Header *hdr = (const struct CCBPF_Header *)image;
    if (hdr->magic != CCBPF_MAGIC)
        return NULL;

    if (hdr->code_offset + hdr->code_size > len)
        return NULL;

    size_t insn_count = hdr->code_size / sizeof(struct bpf_insn);
    struct bpf_insn *insns = heap_malloc(hdr->code_size);
    if (!insns)
        return NULL;
    memcpy(insns, image + hdr->code_offset, hdr->code_size);

    if (!bpf_validate(insns, (int)insn_count)) {
        printf("bpf_validate: reject, len=%zu\n", insn_count);
        heap_free(insns);
        return NULL;
    }

    struct ccbpf_program *prog = heap_malloc(sizeof(*prog));
    if (!prog) {
        heap_free(insns);
        return NULL;
    }
    memset(prog, 0, sizeof(*prog));

    prog->insns = insns;
    prog->insn_count = insn_count;

    prog->string_count = 0;
    prog->strings = NULL;

    if (hdr->data_size > 0) {
        if (hdr->data_offset + hdr->data_size > len) {
            heap_free(prog->insns);
            heap_free(prog);
            return NULL;
        }

        const uint8_t *p = image + hdr->data_offset;

        int count = 0;
        memcpy(&count, p, sizeof(int));
        p += sizeof(int);

        prog->string_count = count;
        prog->strings = heap_malloc(count * sizeof(char *));
        if (!prog->strings) {
            heap_free(prog->insns);
            heap_free(prog);
            return NULL;
        }
        memset(prog->strings, 0, count * sizeof(char *));

        for (int i = 0; i < count; i++) {
            int slen = 0;
            memcpy(&slen, p, sizeof(int));
            p += sizeof(int);

            char *buf = heap_malloc(slen);
            if (!buf) {
                for (int j = 0; j < i; j++)
                    heap_free(prog->strings[j]);
                heap_free(prog->strings);
                heap_free(prog->insns);
                heap_free(prog);
                return NULL;
            }

            memcpy(buf, p, slen);
            p += slen;

            prog->strings[i] = buf;
        }
    }

    prog->entry = hdr->entry;

    return prog;
}

void ccbpf_unload(struct ccbpf_program *prog)
{
    if (!prog)
        return;

    if (prog->insns)
        heap_free(prog->insns);

    if (prog->data)
        heap_free(prog->data);

    if (prog->strings) {
        for (int i = 0; i < prog->string_count; i++) {
            if (prog->strings[i])
                heap_free(prog->strings[i]);
        }
        heap_free(prog->strings);
    }

    heap_free(prog);
}

uint32_t ccbpf_run_frame(struct ccbpf_program *prog,
                         void *frame,
                         size_t frame_size)
{
    struct ccbpf_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

    unsigned char *p = (unsigned char *)frame;
    unsigned int wirelen = (unsigned int)frame_size;
    unsigned int buflen  = (unsigned int)frame_size;

    for (;;) {
        enum ccbpf_status st =
            ccbpf_vm_step(&ctx, prog, p, wirelen, buflen, 64);

        if (st == CCBPF_FINISHED)
            return ctx.ret;

        if (st == CCBPF_ERROR)
            return 0;

        if (st == CCBPF_MIGRATE)
            continue;
    }
}

void ccbpf_system_init(void)
{
    hashmap_init(&hook_table, 32, HASHMAP_KEY_STRING);
    hashmap_init(&native_table, 32, HASHMAP_KEY_INT);
}