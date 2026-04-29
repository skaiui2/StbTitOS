#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "hardware/uart.h"
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

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            uint32_t mask = -(crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void hex_dump(const char *tag, const uint8_t *buf, int len)
{
    if (tag)
        printf("%s (%d bytes):\n", tag, len);

    for (int i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    if (len % 16 != 0)
        printf("\n");
}

char send_buf[300];
static int uart1_send(void *ctx, const uint8_t *data, size_t len)
{

    (void)ctx;
    memset(send_buf, 0, sizeof(send_buf));

    uint8_t *p = send_buf;
    *(uint32_t *)p = START;
    p += 4;
    memcpy(p, data, len);
    p += len;

    uint32_t crc = crc32_update(0, (const uint8_t *)data, len);
    *(uint32_t *)p = crc;
    p += 4;

    *(uint32_t *)p = CLOSE;
    hex_dump("uart1 send: ", data, len);
    uart_write_blocking(uart1, send_buf, sizeof(send_buf));
    return (int)len;
}


static uint8_t frame[256];

static void uart1_read_frame_blocking(void)
{
    int cnt = 0;
    while (cnt < 256) {
        if (uart_is_readable(uart1)) {
            frame[cnt++] = uart_getc(uart1);
        }
    }
}

static uint8_t rpc_buf[256];
static void process_uart1_frame(void)
{
    uart1_read_frame_blocking();

    hex_dump("UART1 FRAME", frame, 256);
    
    int start = -1;
    int end   = -1;

    for (int i = 0; i + 4 <= 256; i++) {
        uint32_t magic = *(uint32_t *)&frame[i];
        if (magic == START) {
            start = i + 4;
        }
        if (magic == CLOSE && start >= 0 && i > start) {
            end = i;
            break;
        }
    }

    if (start < 0 || end < start + 4)
        return;

    int crc_pos     = end - 4;
    int payload_len = crc_pos - start;

    const uint8_t *payload = &frame[start];
    uint32_t recv_crc      = *(uint32_t *)&frame[crc_pos];
    uint32_t calc_crc      = crc32_update(0, payload, payload_len);

    if (recv_crc != calc_crc) {
        printf("CRC ERROR\n");
        return;
    }

    struct ccnet_hdr *ch = (struct ccnet_hdr *)payload;
    uint16_t packet_len  = ntohs(ch->len) + sizeof(struct ccnet_hdr);

    ccnet_input(NULL, payload, packet_len);

    int rn = scp_recv(1, rpc_buf, sizeof(rpc_buf));
    if (rn > 0) {
        rpc_on_data(g_rpc_transport, rpc_buf, rn);
        printf("%.*s\n", rn, rpc_buf);
    }
}


static void uart1_close(void *ctx)
{
}

static int scp_ccnet_send(void *user, const void *buf, size_t len)
{
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
    while (1) {
        task_enter();
        process_uart1_frame();
        task_exit();
    }
}

static void task_shell(void *p)
{
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

    if (cyw43_arch_init()) while (1);

    printf("Pico leader boot ok\r\n");

    struct superblock sb;
    fs_port_init();
    fs_port_mount(&sb);

    multicore_launch_core1(core1_main);

    ccnet_init(NODEA, NODE_COUNT);
    ccnet_register_node_link(NODEA, scp_input);
    printf("scp_input:%u\r\n", scp_input);
    ccnet_register_node_link(NODEB, uart1_send);

    ccnet_graph_set_edge(NODEA, NODEB, 1);
    ccnet_graph_set_edge(NODEB, NODEA, 1);
    ccnet_build_routing_table();

    scp_init(4);
    scp_stream_alloc(&scp_trans, NODEA, NODEB);

    rpc_init(16);
    g_rpc_transport = rpc_trans_class_create(
        (void *)scp_send,
        NULL,
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

    //task_create(task_shell, 1024, NULL, 100, 50, &t_shell);
    task_create(task_uart1_poll, 512, NULL, 5, 20, &t_uart1_poll);
    scheduler_start();

    while (1) {};
}
