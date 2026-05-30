#include "comm.h"
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "schedule.h"

comm_t *comm;

static uart_inst_t *s_uart = NULL;

static void comm_uart_putc(void *ctx, char c)
{
    uart_inst_t *u = (uart_inst_t *)ctx;
    uart_putc_raw(u, c);
}

static char comm_uart_getc(void *ctx)
{
    uart_inst_t *u = (uart_inst_t *)ctx;

    while (!uart_is_readable(u)) {
        task_delay(2);   
    }

    return (char)uart_getc(u);
}

static void comm_uart_write(void *ctx, const char *buf, int len)
{
    uart_inst_t *u = (uart_inst_t *)ctx;
    for (int i = 0; i < len; i++)
        uart_putc_raw(u, buf[i]);
}

static int comm_uart_peek(void *ctx)
{
    uart_inst_t *u = (uart_inst_t *)ctx;
    if (uart_is_readable(u))
        return uart_getc(u);
    return -1;
}

static comm_t uart_comm = {
    .putc  = comm_uart_putc,
    .getc  = comm_uart_getc,
    .write = comm_uart_write,
    .peek  = comm_uart_peek,
    .ctx   = NULL,
};

void comm_init_uart(void *uart_id)
{
    s_uart = (uart_inst_t *)uart_id;
    uart_comm.ctx = s_uart;
    comm = &uart_comm;
}
