/*
 * Copyright (c) 1990, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from the Stanford/CMU enet packet filter,
 * (net/enet.c) distributed as part of 4.3BSD, and code contributed
 * to Berkeley by Steven McCanne and Van Jacobson both of Lawrence
 * Berkeley Laboratory.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *      @(#)bpf_filter.c	8.1 (Berkeley) 6/10/93
 *
 * static char rcsid[] =
 * "$Header: bpf_filter.c,v 1.16 91/10/27 21:22:35 mccanne Exp $";
 */

#include "cbpf.h"
#include "ccbpf.h"
#include "common.h"
#include "heap.h"
#include <stdlib.h>
#include <memory.h>
#include <stdio.h>

static inline uint16_t extract_short_raw(const void *p)
{
    uint16_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

static inline uint32_t extract_long_raw(const void *p)
{
    uint32_t v;
    memcpy(&v, p, sizeof(v));
    return v;
}

uint32_t native_migrate(struct ccbpf_program *p,
                        uint32_t a0,
                        uint32_t a1,
                        uint32_t a2,
                        uint32_t a3)
{
    return 0;
}

#define EXTRACT_SHORT(p)  extract_short_raw(p)
#define EXTRACT_LONG(p)   extract_long_raw(p)

int ccbpf_ctx_pack(struct ccbpf_ctx *ctx,
                   uint8_t **out_buf,
                   size_t *out_len)
{
    *out_len = sizeof(*ctx);
    uint8_t *buf = heap_malloc(*out_len);
    if (!buf)
        return -1;

    memcpy(buf, ctx, *out_len);
    *out_buf = buf;
    return 0;
}

int ccbpf_ctx_unpack(struct ccbpf_ctx *ctx,
                     const uint8_t *buf,
                     size_t len)
{
    if (len != sizeof(*ctx))
        return -1;

    memcpy(ctx, buf, len);
    return 0;
}

enum ccbpf_status ccbpf_vm_step(struct ccbpf_ctx *ctx,
                                struct ccbpf_program *prog,
                                unsigned char *p,
                                unsigned int wirelen,
                                unsigned int buflen,
                                int max_insn)
{
    uint32_t A = ctx->A;
    uint32_t X = ctx->X;
    uint32_t pc = ctx->pc;
    int k;

    for (int i = 0; i < max_insn; i++) {
        struct bpf_insn *ins = &prog->insns[pc];

        switch (ins->code) {

        default:
            ctx->ret = 0;
            ctx->A = A;
            ctx->X = X;
            ctx->pc = pc;
            return CCBPF_ERROR;

        case BPF_RET | BPF_K:
            ctx->ret = (uint32_t)ins->k;
            ctx->A = A;
            ctx->X = X;
            ctx->pc = pc;
            return CCBPF_FINISHED;

        case BPF_RET | BPF_A:
            ctx->ret = A;
            ctx->A = A;
            ctx->X = X;
            ctx->pc = pc;
            return CCBPF_FINISHED;

        case BPF_LD | BPF_W | BPF_ABS:
            k = ins->k;
            if (k + (int)sizeof(long) > (int)buflen) goto err;
            A = EXTRACT_LONG(&p[k]);
            pc++;
            continue;

        case BPF_LD | BPF_H | BPF_ABS:
            k = ins->k;
            if (k + (int)sizeof(short) > (int)buflen) goto err;
            A = EXTRACT_SHORT(&p[k]);
            pc++;
            continue;

        case BPF_LD | BPF_B | BPF_ABS:
            k = ins->k;
            if (k >= (int)buflen) goto err;
            A = p[k];
            pc++;
            continue;

        case BPF_LD | BPF_W | BPF_LEN:
            A = wirelen;
            pc++;
            continue;

        case BPF_LDX | BPF_W | BPF_LEN:
            X = wirelen;
            pc++;
            continue;

        case BPF_LD | BPF_W | BPF_IND:
            k = (int)X + ins->k;
            if (k + (int)sizeof(long) > (int)buflen) goto err;
            A = EXTRACT_LONG(&p[k]);
            pc++;
            continue;

        case BPF_LD | BPF_H | BPF_IND:
            k = (int)X + ins->k;
            if (k + (int)sizeof(short) > (int)buflen) goto err;
            A = EXTRACT_SHORT(&p[k]);
            pc++;
            continue;

        case BPF_LD | BPF_B | BPF_IND:
            k = (int)X + ins->k;
            if (k >= (int)buflen) goto err;
            A = p[k];
            pc++;
            continue;

        case BPF_LDX | BPF_MSH | BPF_B:
            k = ins->k;
            if (k >= (int)buflen) goto err;
            X = (p[k] & 0xf) << 2;
            pc++;
            continue;

        case BPF_LD | BPF_IMM:
            A = ins->k;
            pc++;
            continue;

        case BPF_LDX | BPF_IMM:
            X = ins->k;
            pc++;
            continue;

        case BPF_LD | BPF_MEM: {
            size_t off = ins->k;
            if (off + sizeof(uint32_t) > CCBPF_STACK_SIZE) goto err;
            memcpy(&A, &ctx->mem[off], sizeof(uint32_t));
            pc++;
            continue;
        }

        case BPF_LDX | BPF_MEM: {
            size_t off = ins->k;
            if (off + sizeof(uint32_t) > CCBPF_STACK_SIZE) goto err;
            memcpy(&X, &ctx->mem[off], sizeof(uint32_t));
            pc++;
            continue;
        }

        case BPF_ST: {
            size_t off = ins->k;
            if (off + sizeof(uint32_t) > CCBPF_STACK_SIZE) goto err;
            memcpy(&ctx->mem[off], &A, sizeof(uint32_t));
            pc++;
            continue;
        }

        case BPF_STX: {
            size_t off = ins->k;
            if (off + sizeof(uint32_t) > CCBPF_STACK_SIZE) goto err;
            memcpy(&ctx->mem[off], &X, sizeof(uint32_t));
            pc++;
            continue;
        }

        case BPF_JMP | BPF_JA:
            pc += ins->k + 1;
            continue;

        case BPF_JMP | BPF_JGT | BPF_K:
            pc += (A > (uint32_t)ins->k) ? (ins->jt + 1) : (ins->jf + 1);
            continue;

        case BPF_JMP | BPF_JGE | BPF_K:
            pc += (A >= (uint32_t)ins->k) ? (ins->jt + 1) : (ins->jf + 1);
            continue;

        case BPF_JMP | BPF_JEQ | BPF_K:
            pc += (A == (uint32_t)ins->k) ? (ins->jt + 1) : (ins->jf + 1);
            continue;

        case BPF_JMP | BPF_JSET | BPF_K:
            pc += (A & (uint32_t)ins->k) ? (ins->jt + 1) : (ins->jf + 1);
            continue;

        case BPF_JMP | BPF_JGT | BPF_X:
            pc += (A > X) ? (ins->jt + 1) : (ins->jf + 1);
            continue;

        case BPF_JMP | BPF_JGE | BPF_X:
            pc += (A >= X) ? (ins->jt + 1) : (ins->jf + 1);
            continue;

        case BPF_JMP | BPF_JEQ | BPF_X:
            pc += (A == X) ? (ins->jt + 1) : (ins->jf + 1);
            continue;

        case BPF_JMP | BPF_JSET | BPF_X:
            pc += (A & X) ? (ins->jt + 1) : (ins->jf + 1);
            continue;

        case BPF_ALU | BPF_ADD | BPF_X:
            A += X;
            pc++;
            continue;

        case BPF_ALU | BPF_SUB | BPF_X:
            A -= X;
            pc++;
            continue;

        case BPF_ALU | BPF_MUL | BPF_X:
            A *= X;
            pc++;
            continue;

        case BPF_ALU | BPF_DIV | BPF_X:
            if (X == 0) goto err;
            A /= X;
            pc++;
            continue;

        case BPF_ALU | BPF_MOD | BPF_X:
            if (X == 0) goto err;
            A %= X;
            pc++;
            continue;

        case BPF_ALU | BPF_MOD | BPF_K:
            if (ins->k == 0) goto err;
            A %= ins->k;
            pc++;
            continue;

        case BPF_ALU | BPF_AND | BPF_X:
            A &= X;
            pc++;
            continue;

        case BPF_ALU | BPF_OR | BPF_X:
            A |= X;
            pc++;
            continue;

        case BPF_ALU | BPF_LSH | BPF_X:
            A <<= X;
            pc++;
            continue;

        case BPF_ALU | BPF_RSH | BPF_X:
            A >>= X;
            pc++;
            continue;

        case BPF_ALU | BPF_ADD | BPF_K:
            A += ins->k;
            pc++;
            continue;

        case BPF_ALU | BPF_SUB | BPF_K:
            A -= ins->k;
            pc++;
            continue;

        case BPF_ALU | BPF_MUL | BPF_K:
            A *= ins->k;
            pc++;
            continue;

        case BPF_ALU | BPF_DIV | BPF_K:
            if (ins->k == 0) goto err;
            A /= ins->k;
            pc++;
            continue;

        case BPF_ALU | BPF_AND | BPF_K:
            A &= ins->k;
            pc++;
            continue;

        case BPF_ALU | BPF_OR | BPF_K:
            A |= ins->k;
            pc++;
            continue;

        case BPF_ALU | BPF_LSH | BPF_K:
            A <<= ins->k;
            pc++;
            continue;

        case BPF_ALU | BPF_RSH | BPF_K:
            A >>= ins->k;
            pc++;
            continue;

        case BPF_ALU | BPF_NEG:
            A = (uint32_t)(-((int32_t)A));
            pc++;
            continue;

        case BPF_MISC | BPF_TAX:
            X = A;
            pc++;
            continue;

        case BPF_MISC | BPF_TXA:
            A = X;
            pc++;
            continue;

        case BPF_MISC | BPF_COP: {
            int func_id = ins->k;
            struct native_entry *e =
                hashmap_get(&native_table, (void*)(uintptr_t)func_id);
            if (!e) {
                A = 0;
                pc++;
                continue;
            }

            uint32_t a0 = A;
            uint32_t a1 = X;
            uint32_t a2 = 0;
            uint32_t a3 = 0;

            if (e->argc > 2)
                memcpy(&a2, &ctx->mem[0], sizeof(uint32_t));
            if (e->argc > 3)
                memcpy(&a3, &ctx->mem[4], sizeof(uint32_t));

            A = e->fn(prog, a0, a1, a2, a3);

            if (e->fn == native_migrate) {
                ctx->A = A;
                ctx->X = X;
                ctx->pc = pc + 1;
                return CCBPF_MIGRATE;
            }

            pc++;
            continue;
        }
        }
    }

    ctx->A = A;
    ctx->X = X;
    ctx->pc = pc;
    return CCBPF_OK;

err:
    ctx->ret = 0;
    ctx->A = A;
    ctx->X = X;
    ctx->pc = pc;
    return CCBPF_ERROR;
}

enum ccbpf_status ccbpf_vm_run(struct ccbpf_ctx *ctx,
             struct ccbpf_program *prog,
             unsigned char *p,
             unsigned int wirelen,
             unsigned int buflen)
{
    for (;;) {
        enum ccbpf_status st =
            ccbpf_vm_step(ctx, prog, p, wirelen, buflen, 64);
        if (st != CCBPF_OK)
            return st;
    }
}

unsigned int ccbpf_vm_exec_ctx(struct ccbpf_program *prog,
                  unsigned char *p,
                  unsigned int wirelen,
                  unsigned int buflen)
{
    struct ccbpf_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));

    enum ccbpf_status st =
        ccbpf_vm_run(&ctx, prog, p, wirelen, buflen);

    return ctx.ret;
}

int bpf_validate(struct bpf_insn *f, int len)
{
    int i;
    struct bpf_insn *p;

    for (i = 0; i < len; ++i) {
        p = &f[i];

        if (BPF_CLASS(p->code) == BPF_JMP) {
            int from = i + 1;

            if (BPF_OP(p->code) == BPF_JA) {
                if (from + p->k >= len)
                    return 0;
            } else {
                if (from + p->jt >= len || from + p->jf >= len)
                    return 0;
            }
        }

        if (BPF_CLASS(p->code) == BPF_ST ||
            (BPF_CLASS(p->code) == BPF_LD &&
             (p->code & 0xe0) == BPF_MEM)) {
            if (p->k < 0)
                return 0;
            if ((unsigned)p->k + sizeof(uint32_t) > CCBPF_STACK_SIZE)
                return 0;
        }

        if (p->code == (BPF_ALU|BPF_DIV|BPF_K) && p->k == 0)
            return 0;
    }

    return BPF_CLASS(f[len - 1].code) == BPF_RET;
}
