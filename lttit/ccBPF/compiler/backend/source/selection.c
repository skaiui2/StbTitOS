#include <stdlib.h>
#include "ir.h"
#include "layout.h"
#include "bpf_builder.h"
#include "cbpf.h"  

/*
 * IR_MOVE: t_dst = imm
 */
void lower_move(const struct backend_layout *l,
                struct bpf_builder *b, struct IR *ir)
{
    int dst = temp_slot(l, ir->dst);

    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(BPF_LD | BPF_IMM, ir->src1));
    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(BPF_ST, dst));
}

void lower_binop(const struct backend_layout *l,
                 struct bpf_builder *b, struct IR *ir)
{
    int dst   = temp_slot(l, ir->dst);
    int a     = temp_slot(l, ir->src1);
    int bslot = temp_slot(l, ir->src2);

    uint16_t op;

    switch (ir->op) {
    case IR_ADD:
        op = BPF_ALU | BPF_ADD | BPF_X;
        break;
    case IR_SUB:
        op = BPF_ALU | BPF_SUB | BPF_X;
        break;
    case IR_MUL:
        op = BPF_ALU | BPF_MUL | BPF_X;
        break;
    case IR_DIV:
        op = BPF_ALU | BPF_DIV | BPF_X;
        break;
    case IR_MOD:  
        op = BPF_ALU | BPF_MOD | BPF_X;
        break;
    default:
        fprintf(stderr, "lower_binop: unknown IR op=%d\n", ir->op);
        abort();
    }

    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(BPF_LD  | BPF_MEM, a));

    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(BPF_LDX | BPF_MEM, bslot));

    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(op, 0));

    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(BPF_ST, dst));
}

/*
 * IR_LOAD: t_dst = arr[base + index]
 */
void lower_load(const struct backend_layout *l,
                struct bpf_builder *b, struct IR *ir)
{
    int dst  = temp_slot(l, ir->dst);
    int base = map_array_base(l, ir->array_base);
    int slot = base + ir->array_index;

    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(BPF_LD | BPF_MEM, slot));
    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(BPF_ST, dst));
}

/*
 * IR_STORE: arr[base + index] = t_src
 */
void lower_store(const struct backend_layout *l,
                 struct bpf_builder *b, struct IR *ir)
{
    int src  = temp_slot(l, ir->src1);
    int base = map_array_base(l, ir->array_base);
    int slot = base + ir->array_index;

    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(BPF_LD | BPF_MEM, src));
    bpf_builder_emit(b,
        (struct bpf_insn)BPF_STMT(BPF_ST, slot));
}
