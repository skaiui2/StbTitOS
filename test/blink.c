#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "heap.h"
#include "schedule.h"
#include "shell.h"
#include "fs_port.h"
#include "comm.h"
#include "timer.h"
#include "rpc.h"
#include "cluster.h"
#include "vfs.h"
#include "uf.h"
#include "ccnet.h"
#include "scp.h"
#include "common.h"
#include "sem.h"

#define NODEA 1
#define NODEB 2
#define NODE_COUNT 3

#define UART0_TX 0
#define UART0_RX 1
#define UART1_TX 4
#define UART1_RX 5

#define UART_BAUD 115200

#define START 0xA55AA55A
#define CLOSE 0xDEAD5A5A

TaskHandle_t t_shell;
TaskHandle_t t_uart1_poll;

static struct rpc_transport_class *g_rpc_transport;

static uint8_t send_buf[256];

static int uart1_send(void *ctx, const uint8_t *data, size_t len)
{
    (void)ctx;
    memset(send_buf, 0, sizeof(send_buf));
    uint8_t *p = send_buf;
    *(uint32_t *)p = START;
    p += 4;
    memcpy(p, data, len);
    p += len;
    *(uint32_t *)p = CLOSE;
    uart_write_blocking(uart1, send_buf, sizeof(send_buf));
    return (int)len;
}

uint8_t frame[256];
volatile int frame_pos = 0;
semaphore_handle sem_uart1_frame;

static void on_uart1_irq(void)
{
    while (uart_is_readable(uart1)) {
        uint8_t ch = uart_getc(uart1);
        frame[frame_pos++] = ch;
        if (frame_pos >= 256) {
            frame_pos = 0;
            semaphore_release(sem_uart1_frame);
        }
    }
}

static void uart1_irq_init(void)
{
    sem_uart1_frame = semaphore_create(0);
    irq_set_exclusive_handler(UART1_IRQ, on_uart1_irq);
    irq_set_enabled(UART1_IRQ, true);
    uart_set_irq_enables(uart1, true, false);
}

static uint8_t rpc_buf[256];

static void process_uart1_frame(void)
{
    int start = -1;
    int end   = -1;
    for (int i = 0; i + 4 <= 256; i++) {
        uint32_t magic = *(uint32_t *)&frame[i];
        if (magic == START)
            start = i + 4;
        if (magic == CLOSE && start >= 0 && i > start) {
            end = i;
            break;
        }
    }

    const uint8_t *payload = &frame[start];
    struct ccnet_hdr *ch = (struct ccnet_hdr *)payload;
    uint16_t packet_len  = ntohs(ch->len) + sizeof(struct ccnet_hdr);
    ccnet_input(NULL, (void *)payload, packet_len);
    int rn = scp_recv(NODEA, rpc_buf, sizeof(rpc_buf));
    if (rn > 0) {
        rpc_on_data(g_rpc_transport, rpc_buf, (size_t)rn);
    }
}

static int scp_ccnet_send(void *user, const void *buf, size_t len)
{
    (void)user;
    struct ccnet_send_parameter csp = {
        .dst = NODEB,
        .ttl = CCNET_TTL_DEFAULT,
        .type = 1,
    };
    return ccnet_output(&csp, (void *)buf, (int)len);
}

static struct scp_transport_class scp_trans = {
    .send  = scp_ccnet_send,
    .recv  = NULL,
    .close = NULL,
    .user  = NULL,
};

static void task_uart1_poll(void *p)
{
    (void)p;
    while (1) {
        if (semaphore_take(sem_uart1_frame, 0xFFFF) == true) {
            process_uart1_frame();
        }
    }
}

static void task_shell(void *p)
{
    (void)p;
    while (1) {
        task_enter();
        shell_main();
        task_exit();
    }
}

static void core1_main(void)
{
    sleep_ms(1500);
    while (1) {
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        sleep_ms(1000);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        sleep_ms(1000);
    }
}

int main()
{
    stdio_init_all();
    uart_init(uart0, UART_BAUD);
    gpio_set_function(UART0_TX, GPIO_FUNC_UART);
    gpio_set_function(UART0_RX, GPIO_FUNC_UART);
    stdio_uart_init_full(uart0, UART_BAUD, UART0_TX, UART0_RX);
    comm_init_uart(uart0);
    uart_init(uart1, UART_BAUD);
    gpio_set_function(UART1_TX, GPIO_FUNC_UART);
    gpio_set_function(UART1_RX, GPIO_FUNC_UART);
    uart1_irq_init();
    if (cyw43_arch_init())
        while (1) {}
    printf("Pico leader boot ok\n");
    struct superblock sb;
    fs_port_init();
    fs_port_mount(&sb);
    multicore_launch_core1(core1_main);
    ccnet_init(NODEA, NODE_COUNT);
    ccnet_register_node_link(NODEA, scp_input);
    ccnet_register_node_link(NODEB, (void *)uart1_send);
    ccnet_graph_set_edge(NODEA, NODEB, 1);
    ccnet_graph_set_edge(NODEB, NODEA, 1);
    ccnet_build_routing_table();
    scp_init(4);
    scp_stream_alloc(&scp_trans, NODEA, NODEB);
    rpc_init(16);
    g_rpc_transport = rpc_trans_class_create(
        (void *)scp_send,
        (void *)scp_recv,
        NULL,
        NULL
    );
    cluster_init();
    vfs_init("nodeA");
    struct vnode *root = vfs_mkdirs("root");
    vnode_set_ops(root, cluster_root_ops());
    rpc_set_handler(uf_handle);
    
    scheduler_init();
    timer_init();
    timer_create((void *)scp_timer_process, 10, run);
    task_create(task_shell, 1024, NULL, 100, 50, &t_shell);
    task_create(task_uart1_poll, 512, NULL, 0, 10, &t_uart1_poll);
    scheduler_start();
    while (1) {}
}
