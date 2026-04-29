#include "comm.h"
#include "stm32f1xx_hal.h"

comm_t *comm;

static UART_HandleTypeDef *s_uart = NULL;

static void uart_putc(void *ctx, char c)
{
    UART_HandleTypeDef *u = (UART_HandleTypeDef *)ctx;
    uint8_t b = (uint8_t)c;
    HAL_UART_Transmit(u, &b, 1, HAL_MAX_DELAY);
}

static char uart_getc(void *ctx)
{
    UART_HandleTypeDef *u = (UART_HandleTypeDef *)ctx;
    uint8_t b;
    HAL_UART_Receive(u, &b, 1, HAL_MAX_DELAY);
    return (char)b;
}

static void uart_write(void *ctx, const char *buf, int len)
{
    UART_HandleTypeDef *u = (UART_HandleTypeDef *)ctx;
    HAL_UART_Transmit(u, (uint8_t *)buf, (uint16_t)len, HAL_MAX_DELAY);
}

static int uart_peek(void *ctx)
{
    (void)ctx;
    return -1;
}

static comm_t uart_comm = {
        .putc  = uart_putc,
        .getc  = uart_getc,
        .write = uart_write,
        .peek  = uart_peek,
        .ctx   = NULL,
};

void comm_init_uart(void *huart_handle)
{
    s_uart = (UART_HandleTypeDef *)huart_handle;
    uart_comm.ctx = s_uart;
    comm = &uart_comm;
}
