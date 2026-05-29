#include "ir_lowering.h"
#include "layout.h"
#include "lexer.h"
#include "selection.h"
#include "controlflow.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

void ir_lower_program(struct IR *head, int label_count,
                      struct bpf_builder *b)
{
    struct backend_layout layout = default_bpf_layout();

    int pj_count = 0;
    struct pending *pj = mg_region_alloc(backend_region,
                                         MAX_PENDING_JUMPS * sizeof(*pj));

    int *label_pc = mg_region_alloc(backend_region,
                                    label_count * sizeof(int));
    memset(label_pc, 0, label_count * sizeof(int));

    for (struct IR *ir = head; ir; ir = ir->next) {
        switch (ir->op) {

        case IR_MOVE:
            lower_move(&layout, b, ir);
            break;

        case IR_ADD:
        case IR_SUB:
        case IR_MUL:
        case IR_DIV:
        case IR_MOD:
            lower_binop(&layout, b, ir);
            break;

        case IR_RET: {
            int slot = temp_slot(&layout, ir->src1);
            bpf_builder_emit(b,
                (struct bpf_insn)BPF_STMT(BPF_LD | BPF_MEM, slot));
            bpf_builder_emit(b,
                (struct bpf_insn)BPF_STMT(BPF_RET | BPF_A, 0));
            break;
        }

        case IR_LOAD_CTX: {
            int offset = ir->src1;
            int size   = ir->src2;
            int dst    = temp_slot(&layout, ir->dst);

            switch (size) {
            case 1:
                bpf_builder_emit(b,
                    (struct bpf_insn)BPF_STMT(BPF_LD | BPF_B | BPF_ABS, offset));
                break;
            case 2:
                bpf_builder_emit(b,
                    (struct bpf_insn)BPF_STMT(BPF_LD | BPF_H | BPF_ABS, offset));
                break;
            case 4:
                bpf_builder_emit(b,
                    (struct bpf_insn)BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offset));
                break;
            default:
                fprintf(stderr, "invalid IR_LOAD_CTX size\n");
                exit(1);
            }

            bpf_builder_emit(b,
                (struct bpf_insn)BPF_STMT(BPF_ST, dst));
            break;
        }

        case IR_NATIVE_CALL: {
            int dst_slot = temp_slot(&layout, ir->dst);

            for (int i = 0; i < ir->argc; i++) {
                int arg_slot = temp_slot(&layout, ir->args[i]);

                if (i == 0) {
                    bpf_builder_emit(b,
                        (struct bpf_insn)BPF_STMT(BPF_LD | BPF_MEM, arg_slot));
                } else if (i == 1) {
                    bpf_builder_emit(b,
                        (struct bpf_insn)BPF_STMT(BPF_LDX | BPF_MEM, arg_slot));
                } else if (i == 2) {
                    bpf_builder_emit(b,
                        (struct bpf_insn)BPF_STMT(BPF_LD | BPF_MEM, arg_slot));
                    bpf_builder_emit(b,
                        (struct bpf_insn)BPF_STMT(BPF_ST, 0));
                } else if (i == 3) {
                    bpf_builder_emit(b,
                        (struct bpf_insn)BPF_STMT(BPF_LD | BPF_MEM, arg_slot));
                    bpf_builder_emit(b,
                        (struct bpf_insn)BPF_STMT(BPF_ST, 4));
                } else {
                    fprintf(stderr, "too many args for native call\n");
                    exit(1);
                }
            }

            if (ir->argc > 0) {
                int arg0_slot = temp_slot(&layout, ir->args[0]);
                bpf_builder_emit(b,
                    (struct bpf_insn)BPF_STMT(BPF_LD | BPF_MEM, arg0_slot));
            }

            bpf_builder_emit(b,
                (struct bpf_insn)BPF_STMT(BPF_MISC | BPF_COP,
                                          ir->native_id));

            bpf_builder_emit(b,
                (struct bpf_insn)BPF_STMT(BPF_ST, dst_slot));
            break;
        }

        case IR_LOAD: {
            int dst_slot = temp_slot(&layout, ir->dst);
            int offset   = ir->array_base;

            bpf_builder_emit(b,
                (struct bpf_insn)BPF_STMT(BPF_LD | BPF_MEM, offset));

            bpf_builder_emit(b,
                (struct bpf_insn)BPF_STMT(BPF_ST, dst_slot));
            break;
        }

        case IR_STORE: {
            int src_slot = temp_slot(&layout, ir->src1);
            int offset   = ir->array_base;

            bpf_builder_emit(b,
                (struct bpf_insn)BPF_STMT(BPF_LD | BPF_MEM, src_slot));

            bpf_builder_emit(b,
                (struct bpf_insn)BPF_STMT(BPF_ST, offset));
            break;
        }

        case IR_IF_FALSE:
            lower_if_false(&layout, b, ir,
                           &pj, &pj_count);
            break;

        case IR_GOTO:
            lower_goto(b, ir,
                       &pj, &pj_count);
            break;

        case IR_LABEL:
            lower_label(label_pc, b, ir);
            break;

        default:
            printf("ir_lowering abort\r\n");
            abort();
        }
    }

    patch_jumps(b, pj, pj_count, label_pc);

    int n = bpf_builder_count(b);
    struct bpf_insn *prog = bpf_builder_data(b);
    printf("BPF program (%d insns):\n", n);
    for (int i = 0; i < n; i++) {
        printf("%3d: code=0x%04x jt=%u jf=%u k=%ld\n",
               i, prog[i].code, prog[i].jt, prog[i].jf, prog[i].k);
    }
}

void ir_free()
{
    mg_region_destroy(ir_region);
}