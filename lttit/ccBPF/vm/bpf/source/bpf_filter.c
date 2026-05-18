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

#define EXTRACT_SHORT(p)  extract_short_raw(p)
#define EXTRACT_LONG(p)   extract_long_raw(p)


#define CCBPF_STACK_SIZE (10*1024)
uint8_t mem[CCBPF_STACK_SIZE];
unsigned int ccbpf_vm_exec(struct ccbpf_program *prog,
                    struct bpf_insn *pc,
                    unsigned char *p,
                    unsigned int wirelen,
                    unsigned int buflen)
{
    uint32_t A = 0, X = 0;
    int k;
    memset(mem, 0, sizeof(mem));

    if (pc == 0)
        return (unsigned int)-1;

    --pc;
    for (;;) {
        ++pc;
        switch (pc->code) {

        default:
            return 0;

        case BPF_RET | BPF_K:
            return (unsigned int)pc->k;

        case BPF_RET | BPF_A:
            return (unsigned int)A;

        case BPF_LD | BPF_W | BPF_ABS:
            k = pc->k;
            if (k + sizeof(long) > buflen)
                return 0;
            A = EXTRACT_LONG(&p[k]);
            continue;

        case BPF_LD | BPF_H | BPF_ABS:
            k = pc->k;
            if (k + sizeof(short) > buflen)
                return 0;
            A = EXTRACT_SHORT(&p[k]);
            continue;

        case BPF_LD | BPF_B | BPF_ABS:
            k = pc->k;
            if (k >= buflen)
                return 0;
            A = p[k];
            continue;

        case BPF_LD | BPF_W | BPF_LEN:
            A = wirelen;
            continue;

        case BPF_LDX | BPF_W | BPF_LEN:
            X = wirelen;
            continue;

        case BPF_LD | BPF_W | BPF_IND:
            k = X + pc->k;
            if (k + sizeof(long) > buflen)
                return 0;
            A = EXTRACT_LONG(&p[k]);
            continue;

        case BPF_LD | BPF_H | BPF_IND:
            k = X + pc->k;
            if (k + sizeof(short) > buflen)
                return 0;
            A = EXTRACT_SHORT(&p[k]);
            continue;

        case BPF_LD | BPF_B | BPF_IND:
            k = X + pc->k;
            if (k >= buflen)
                return 0;
            A = p[k];
            continue;

        case BPF_LDX | BPF_MSH | BPF_B:
            k = pc->k;
            if (k >= buflen)
                return 0;
            X = (p[pc->k] & 0xf) << 2;
            continue;

        case BPF_LD | BPF_IMM:
            A = pc->k;
            continue;

        case BPF_LDX | BPF_IMM:
            X = pc->k;
            continue;

        case BPF_LD | BPF_MEM: {
            size_t off = pc->k;
            if (off + sizeof(uint32_t) > CCBPF_STACK_SIZE)
                return 0;
            memcpy(&A, &mem[off], sizeof(uint32_t));
            continue;
        }

        case BPF_LDX | BPF_MEM: {
            size_t off = pc->k;
            if (off + sizeof(uint32_t) > CCBPF_STACK_SIZE)
                return 0;
            memcpy(&X, &mem[off], sizeof(uint32_t));
            continue;
        }

        case BPF_ST: {
            size_t off = pc->k;
            if (off + sizeof(uint32_t) > CCBPF_STACK_SIZE)
                return 0;
            memcpy(&mem[off], &A, sizeof(uint32_t));
            continue;
        }

        case BPF_STX: {
            size_t off = pc->k;
            if (off + sizeof(uint32_t) > CCBPF_STACK_SIZE)
                return 0;
            memcpy(&mem[off], &X, sizeof(uint32_t));
            continue;
        }

        case BPF_JMP | BPF_JA:
            pc += pc->k;
            continue;

        case BPF_JMP | BPF_JGT | BPF_K:
            pc += (A > pc->k) ? pc->jt : pc->jf;
            continue;

        case BPF_JMP | BPF_JGE | BPF_K:
            pc += (A >= pc->k) ? pc->jt : pc->jf;
            continue;

        case BPF_JMP | BPF_JEQ | BPF_K:
            pc += (A == pc->k) ? pc->jt : pc->jf;
            continue;

        case BPF_JMP | BPF_JSET | BPF_K:
            pc += (A & pc->k) ? pc->jt : pc->jf;
            continue;

        case BPF_JMP | BPF_JGT | BPF_X:
            pc += (A > X) ? pc->jt : pc->jf;
            continue;

        case BPF_JMP | BPF_JGE | BPF_X:
            pc += (A >= X) ? pc->jt : pc->jf;
            continue;

        case BPF_JMP | BPF_JEQ | BPF_X:
            pc += (A == X) ? pc->jt : pc->jf;
            continue;

        case BPF_JMP | BPF_JSET | BPF_X:
            pc += (A & X) ? pc->jt : pc->jf;
            continue;

        case BPF_ALU | BPF_ADD | BPF_X:
            A += X;
            continue;

        case BPF_ALU | BPF_SUB | BPF_X:
            A -= X;
            continue;

        case BPF_ALU | BPF_MUL | BPF_X:
            A *= X;
            continue;

        case BPF_ALU | BPF_DIV | BPF_X:
            if (X == 0)
                return 0;
            A /= X;
            continue;

        case BPF_ALU | BPF_AND | BPF_X:
            A &= X;
            continue;

        case BPF_ALU | BPF_OR | BPF_X:
            A |= X;
            continue;

        case BPF_ALU | BPF_LSH | BPF_X:
            A <<= X;
            continue;

        case BPF_ALU | BPF_RSH | BPF_X:
            A >>= X;
            continue;;

        case BPF_ALU | BPF_ADD | BPF_K:
            A += pc->k;
            continue;

        case BPF_ALU | BPF_SUB | BPF_K:
            A -= pc->k;
            continue;

        case BPF_ALU | BPF_MUL | BPF_K:
            A *= pc->k;
            continue;

        case BPF_ALU | BPF_DIV | BPF_K:
            if (pc->k == 0)
                return 0;
            A /= pc->k;
            continue;

        case BPF_ALU | BPF_AND | BPF_K:
            A &= pc->k;
            continue;

        case BPF_ALU | BPF_OR | BPF_K:
            A |= pc->k;
            continue;

        case BPF_ALU | BPF_LSH | BPF_K:
            A <<= pc->k;
            continue;

        case BPF_ALU | BPF_RSH | BPF_K:
            A >>= pc->k;
            continue;

        case BPF_ALU | BPF_NEG:
            A = -A;
            continue;

        case BPF_MISC | BPF_TAX:
            X = A;
            continue;

        case BPF_MISC | BPF_TXA:
            A = X;
            continue;

        case BPF_MISC | BPF_COP: {
            int func_id = pc->k;
            struct native_entry *e =
                hashmap_get(&native_table, (void*)(uintptr_t)func_id);
            if (!e) {
                A = 0;
                continue;
            }

            uint32_t a0 = A;
            uint32_t a1 = X;
            uint32_t a2 = 0;
            uint32_t a3 = 0;

            if (e->argc > 2)
                memcpy(&a2, &mem[0], sizeof(uint32_t));
            if (e->argc > 3)
                memcpy(&a3, &mem[4], sizeof(uint32_t));

            A = e->fn(prog, a0, a1, a2, a3);
            continue;
        }
        }
    }
}


/*
 * Return true if the 'fcode' is a valid filter program.
 * The constraints are that each jump be forward and to a valid
 * code.  The code must terminate with either an accept or reject. 
 * 'valid' is an array for use by the routine (it must be at least
 * 'len' bytes long).  
 *
 * The kernel needs to be able to verify an application's filter code.
 * Otherwise, a bogus program could easily crash the system.
 */
int bpf_validate(struct bpf_insn *f, int len)
{
	register int i;
	register struct bpf_insn *p;

	for (i = 0; i < len; ++i) {
		/*
		 * Check that that jumps are forward, and within 
		 * the code block.
		 */
		p = &f[i];
		if (BPF_CLASS(p->code) == BPF_JMP) {
			register int from = i + 1;

			if (BPF_OP(p->code) == BPF_JA) {
				if (from + p->k >= len)
					return 0;
			}
			else if (from + p->jt >= len || from + p->jf >= len)
				return 0;
		}
		/*
		 * Check that memory operations use valid addresses.
		 */
		if ((BPF_CLASS(p->code) == BPF_ST ||
		     (BPF_CLASS(p->code) == BPF_LD && 
		      (p->code & 0xe0) == BPF_MEM)) &&
		    (p->k >= BPF_MEMWORDS || p->k < 0))
			return 0;
		/*
		 * Check for constant division by 0.
		 */
		if (p->code == (BPF_ALU|BPF_DIV|BPF_K) && p->k == 0)
			return 0;
	}
	return BPF_CLASS(f[len - 1].code) == BPF_RET;
}
