#ifndef COMM_H
#define COMM_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    void (*putc)(void *ctx, char c);
    char (*getc)(void *ctx);
    void (*write)(void *ctx, const char *buf, int len);
    int  (*peek)(void *ctx);
    void *ctx;
} comm_t;

extern comm_t *comm;

void comm_init_uart(void *huart_handle);

static inline void comm_putc(char c)
{
    comm->putc(comm->ctx, c);
}

static inline char comm_getc(void)
{
    return comm->getc(comm->ctx);
}

static inline void comm_write(const char *buf, int len)
{
    comm->write(comm->ctx, buf, len);
}

static inline int comm_peek(void)
{
    if (comm->peek)
        return comm->peek(comm->ctx);
    return -1;
}

#endif
