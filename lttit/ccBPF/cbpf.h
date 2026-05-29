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
 *      @(#)bpf.h	8.1 (Berkeley) 6/10/93
 *
 * @(#) $Header: bpf.h,v 1.24 91/10/27 21:22:32 mccanne Exp $ (LBL)
 */
#ifndef CBPF_H
#define CBPF_H

#include <stdint.h>
#include <stdio.h>
/*
 * The instruction encondings.
 */
/* instruction classes */
#define BPF_CLASS(code) ((code) & 0x07)
#define		BPF_LD		0x00
#define		BPF_LDX		0x01
#define		BPF_ST		0x02
#define		BPF_STX		0x03
#define		BPF_ALU		0x04
#define		BPF_JMP		0x05
#define		BPF_RET		0x06
#define		BPF_MISC	0x07

/* ld/ldx fields */
#define BPF_SIZE(code)	((code) & 0x18)
#define		BPF_W		0x00
#define		BPF_H		0x08
#define		BPF_B		0x10
#define BPF_MODE(code)	((code) & 0xe0)
#define		BPF_IMM 	0x00
#define		BPF_ABS		0x20
#define		BPF_IND		0x40
#define		BPF_MEM		0x60
#define		BPF_LEN		0x80
#define		BPF_MSH		0xa0

/* alu/jmp fields */
#define BPF_OP(code)	((code) & 0xf0)
#define		BPF_ADD		0x00
#define		BPF_SUB		0x10
#define		BPF_MUL		0x20
#define		BPF_DIV		0x30
#define		BPF_OR		0x40
#define		BPF_AND		0x50
#define		BPF_LSH		0x60
#define		BPF_RSH		0x70
#define		BPF_NEG		0x80
#define     BPF_MOD     0x90   // A = A % (K|X)
#define		BPF_JA		0x00
#define		BPF_JEQ		0x10
#define		BPF_JGT		0x20
#define		BPF_JGE		0x30
#define		BPF_JSET	0x40
#define BPF_SRC(code)	((code) & 0x08)
#define		BPF_K		0x00
#define		BPF_X		0x08

/* ret - BPF_K and BPF_X also apply */
#define BPF_RVAL(code)	((code) & 0x18)
#define		BPF_A		0x10

/* misc */
#define BPF_MISCOP(code) ((code) & 0xf8)
#define		BPF_TAX		0x00
#define		BPF_TXA		0x80

#define BPF_COP  0x40   /* custom operation */

        
#define CCBPF_STACK_SIZE (512)

enum ccbpf_status {
    CCBPF_OK,
    CCBPF_FINISHED,
    CCBPF_MIGRATE,
    CCBPF_ERROR
};

/*
 * The instruction data structure.
 */
struct bpf_insn {
	unsigned short	code;
	unsigned char 	jt;
	unsigned char 	jf;
	long	k;
};

struct ccbpf_ctx {
    uint32_t A;
    uint32_t X;
    uint32_t pc;
    uint32_t ret;
    uint8_t  mem[CCBPF_STACK_SIZE];
};

struct ccbpf_program {
    struct bpf_insn *insns; //.text
    size_t insn_count;

    uint8_t *data;     //.data or .radata
    size_t data_size;          

    uint32_t entry;

    int string_count; 
    char **strings;
    void *ctx;
};

/*
 * Macros for insn array initializers.
 */
#define BPF_STMT(code, k) { (unsigned short)(code), 0, 0, k }
#define BPF_JUMP(code, k, jt, jf) { (unsigned short)(code), jt, jf, k }

/*
 * Number of scratch memory words (for BPF_LD|BPF_MEM and BPF_ST).
 */
#define BPF_MEMWORDS 64

int ccbpf_ctx_pack(struct ccbpf_ctx *ctx,
                   uint8_t **out_buf,
                   size_t *out_len);

int ccbpf_ctx_unpack(struct ccbpf_ctx *ctx,
                     const uint8_t *buf,
                     size_t len);

uint32_t native_migrate(struct ccbpf_program *p,
                        uint32_t a0,
                        uint32_t a1,
                        uint32_t a2,
                        uint32_t a3);

enum ccbpf_status ccbpf_vm_step(struct ccbpf_ctx *ctx,
              struct ccbpf_program *prog,
              unsigned char *p,
              unsigned int wirelen,
              unsigned int buflen,
              int max_insn);

enum ccbpf_status ccbpf_vm_run(struct ccbpf_ctx *ctx,
             struct ccbpf_program *prog,
             unsigned char *p,
             unsigned int wirelen,
             unsigned int buflen);

unsigned int ccbpf_vm_exec_ctx(struct ccbpf_program *prog,
                  unsigned char *p,
                  unsigned int wirelen,
                  unsigned int buflen);

int bpf_validate(struct bpf_insn *f, int len);

#endif
