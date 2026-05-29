#include "ir.h"
#include "lexer.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

struct IR *ir_head = NULL;
struct IR *ir_tail = NULL;

void ir_init(void)
{
    ir_head = NULL;
    ir_tail = NULL;
}

void ir_mes_get(struct ir_mes *im)
{
    int label_count = 0;
    for (struct IR *x = ir_head; x; x = x->next)
        if (x->label > label_count)
            label_count = x->label;
    label_count++;
    im->ir_head = ir_head;
    im->label_count = label_count;
}

void ir_emit(struct IR ir)
{
    struct IR *slot = mg_region_alloc(ir_region, sizeof(struct IR));
    if (!slot) {
        fprintf(stderr, "IR alloc failed\n");
        exit(1);
    }

    *slot = ir;
    slot->next = NULL;

    if (!ir_head) {
        ir_head = slot;
        ir_tail = slot;
    } else {
        ir_tail->next = slot;
        ir_tail = slot;
    }

    switch (ir.op) {
        case IR_MOVE:
            printf("[IR] MOVE  t%d <- %d\n", ir.dst, ir.src1);
            break;
        case IR_ADD:
            printf("[IR] ADD   t%d <- t%d + t%d\n", ir.dst, ir.src1, ir.src2);
            break;
        case IR_SUB:
            printf("[IR] SUB   t%d <- t%d - t%d\n", ir.dst, ir.src1, ir.src2);
            break;
        case IR_MUL:
            printf("[IR] MUL   t%d <- t%d * t%d\n", ir.dst, ir.src1, ir.src2);
            break;
        case IR_DIV:
            printf("[IR] DIV   t%d <- t%d / t%d\n", ir.dst, ir.src1, ir.src2);
            break;
        case IR_MOD:   
            printf("[IR] MOD   t%d <- t%d %% t%d\n", ir.dst, ir.src1, ir.src2);
            break;

        case IR_RET:
            printf("[IR] RET t%d\n", ir.src1);
            break;

        case IR_LOAD_CTX:
            printf("[IR] LOAD_CTX t%d <- CTX[%d]\n", ir.dst, ir.src1);
            break;

        case IR_NATIVE_CALL:
            printf("[IR] NATIVE_CALL func=%d dst=t%d argc=%d (args: t%d ...)\n",
                   ir.native_id, ir.dst, ir.argc,
                   ir.argc > 0 ? ir.args[0] : -1);
            break;

        case IR_LOAD:
            printf("[IR] LOAD  t%d <- MEM[%d + t%d * %d]\n",
                   ir.dst, ir.array_base, ir.array_index, ir.array_width);
            break;
        case IR_STORE:
            printf("[IR] STORE MEM[%d + t%d * %d] <- t%d\n",
                   ir.array_base, ir.array_index, ir.array_width, ir.src1);
            break;

        case IR_IF_FALSE: {
            const char *op_str = "?";
            switch (ir.relop) {
                case IR_GT: op_str = ">";  break;
                case IR_GE: op_str = ">="; break;
                case IR_EQ: op_str = "=="; break;
                case IR_NE: op_str = "!="; break;
            }
            printf("[IR] IFFALSE !(t%d %s t%d) goto L%d\n",
                   ir.src1, op_str, ir.src2, ir.label);
            break;
        }

        case IR_GOTO:
            if (ir.label == 0) break;
            printf("[IR] GOTO L%d\n", ir.label);
            break;

        case IR_LABEL:
            printf("[IR] LABEL L%d\n", ir.label);
            break;

        default:
            printf("[IR] ???\n");
            break;
    }
}
