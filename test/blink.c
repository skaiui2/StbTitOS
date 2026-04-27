#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/multicore.h"
#include "heap.h"
#include <stdint.h>
#include <stdlib.h>
#include "schedule.h"
#include "shell.h"
#include "fs_port.h"
#include "comm.h"

TaskHandle_t t_shell;


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

    uart_init(uart0, 115200);
    gpio_set_function(0, GPIO_FUNC_UART);
    gpio_set_function(1, GPIO_FUNC_UART);
    stdio_uart_init_full(uart0, 115200, 0, 1);
    comm_init_uart(uart0);

    if (cyw43_arch_init()) {
        while (1);
    }

    printf("boot ok\r\n");

    struct superblock sb;
    fs_port_init();

    if (fs_port_mount(&sb) != 0) {
        printf("FS mount after format failed!\r\n");
    }

    multicore_launch_core1(core1_main);

    scheduler_init();
    task_create(task_shell, 1024, NULL, 10, 1, &t_shell);
    scheduler_start();

    while (1);
}
