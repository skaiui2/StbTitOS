/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
#include "stm32f1xx_hal.h"
#include "fs_port.h"

#include "stm32f1xx_hal.h"
#include "fs_port.h"
#include "schedule.h"
#include "sem.h"
#include "ccnet.h"
#include "common.h"
#include "shell.h"
#include "scp.h"
#include "timer.h"
#include "rpc.h"
#include "world.h"
#include "heap.h"
#include "cbpf.h"
#include "ccbpf.h"
#include <stdio.h>
#include <memory.h>
#include <stdlib.h>

extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;

#define NODE_ID_A   1
#define NODE_ID_B   2
#define NODE_COUNT  3

#define scp_fd_A 1
#define scp_fd_B 2

struct rpc_transport_class *g_rpc_transport = NULL;

static size_t rpc_scp_send(void *user, const uint8_t *buf, size_t len)
{
    (void)user;
    return (size_t)scp_send(scp_fd_B, (void *)buf, (int)len);
}

static size_t rpc_scp_recv(void *user, uint8_t *buf, size_t maxlen)
{
    (void)user;
    (void)buf;
    (void)maxlen;
    return 0;
}

static void rpc_scp_close(void *user)
{
    (void)user;
}

uint8_t send_buf[256];
uint8_t rcv_buf[256];
uint8_t packet[256];
semaphore_handle sem_process;
#define START 0xA55AA55A
#define CLOSE 0xDEAD5A5A

uint8_t appbuf[256];
void process(void)
{
    int start = -1;
    int end   = -1;

    for (uint16_t i = 0; i + 4 <= 256; i++) {
        uint32_t magic = *(uint32_t *)&rcv_buf[i];
        if (magic == START) {
            start = i + 4;
        }
        if (magic == CLOSE && start >= 0 && i > start) {
            end = i;
            break;
        }
    }

    if (start >= 0 && end > start + 4) {
        int payload_len = end - start;

        const uint8_t *payload = &rcv_buf[start];
        memset(packet, 0, sizeof(packet));
        memcpy(packet, payload, payload_len);

        struct ccnet_hdr *ch = (struct ccnet_hdr *)packet;
        uint16_t packet_len  = ntohs(ch->len) + sizeof(struct ccnet_hdr);

        ccnet_input(NULL, packet, packet_len);

        memset(appbuf, 0, sizeof(appbuf));
        int rn = scp_recv(scp_fd_B, appbuf, sizeof(appbuf));
        if (rn > 0) {
            rpc_on_data(g_rpc_transport, appbuf, (size_t)rn);
        }
    }
}

static int nodeB_provider(void *ctx, void *data, size_t len)
{
    (void)ctx;
    memset(send_buf, 0, sizeof(send_buf));

    uint8_t *p = send_buf;
    *(uint32_t *)p = START;
    p += 4;
    memcpy(p, data, len);
    p += len;

    *(uint32_t *)p = CLOSE;
    HAL_UART_Transmit(&huart1, send_buf, sizeof(send_buf), HAL_MAX_DELAY);

    return 0;
}


void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        semaphore_release(sem_process);

        __HAL_UART_CLEAR_OREFLAG(&huart1);
        __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
        HAL_NVIC_EnableIRQ(USART1_IRQn);

        HAL_UART_Receive_IT(&huart1, rcv_buf, 256);
    }
}

void process_rcv(void *ctx)
{
    while (1) {
        if (semaphore_take(sem_process, 0xFFFF) == true) {
            process();
        }
    }
}

TaskHandle_t t1;
TaskHandle_t t_process;

int scp_ccnet_send(void *user, const void *buf, size_t len)
{
    struct ccnet_send_parameter csp = {
            .dst = 1,
            .ttl = CCNET_TTL_DEFAULT,
            .type = 1,
    };
    return ccnet_output(&csp, (void *)buf, (int)len);
}

void timer_excu(void)
{
    scp_timer_process();
}

struct scp_transport_class scp_trans = {
        .send  = scp_ccnet_send,
        .recv  = NULL,
        .close = NULL,
        .user  = NULL,
};

static int led_open(void *self, const char *path, int flags)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    return 0;
}

static int led_close(void *self, int fd)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    return 0;
}

static struct file_ops led_ops = {
        led_open,
        0,
        0,
        0,
        led_close
};

static int led1_open(void *self, const char *path, int flags)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);

    return 0;
}

static int led1_close(void *self, int fd)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    return 0;
}

static struct file_ops led1_ops = {
        led1_open,
        0,
        0,
        0,
        led1_close
};

void led1_register(void)
{
    world_register("root/nodeB/dev/led1", &led1_ops, 0);
}

void led_register(void)
{
    world_register("root/nodeB/dev/led", &led_ops, 0);
}

int remote_region_open(int size)
{
    struct rpc_request req = {0};
    struct rpc_response resp = {0};
    char argbuf[32];
    int fd;

    snprintf(argbuf, sizeof(argbuf), "%d", size);
    req.op = RPC_OP_OPEN;
    req.path = "/root/nodeA/mem/region1";
    req.args = argbuf;

    rpc_call(g_rpc_transport, &req, &resp, 10000);
    fd = atoi(resp.output);
    rpc_free_response(&resp);
    return fd;
}

int remote_region_close(int fd)
{
    struct rpc_request req = {0};
    struct rpc_response resp = {0};
    char argbuf[32];
    int r;

    snprintf(argbuf, sizeof(argbuf), "%d", fd);
    req.op = RPC_OP_CLOSE;
    req.path = "/root/nodeA/mem/region1";
    req.args = argbuf;

    rpc_call(g_rpc_transport, &req, &resp, 10000);
    r = atoi(resp.output);
    rpc_free_response(&resp);
    return r;
}
int remote_region_write_block(int fd, int offset, uint8_t value, int count)
{
    struct rpc_request req = {0};
    struct rpc_response resp = {0};
    char argbuf[64];
    uint8_t data[256];
    int i, r;

    if (count > (int)sizeof(data))
        count = sizeof(data);

    for (i = 0; i < count; i++)
        data[i] = value;

    snprintf(argbuf, sizeof(argbuf),
             "fd=%d,offset=%d,len=%d",
             fd, offset, count);

    req.op       = RPC_OP_WRITE;
    req.path     = "/root/nodeA/mem/region1";
    req.args     = argbuf;
    req.data     = data;
    req.data_len = (uint32_t)count;

    rpc_call(g_rpc_transport, &req, &resp, 10000);
    r = atoi(resp.output ? resp.output : "0");
    rpc_free_response(&resp);
    return r;
}

int remote_region_read(int fd, int offset, uint8_t *buf, int len)
{
    struct rpc_request req = {0};
    struct rpc_response resp = {0};
    char argbuf[64];
    int n;

    snprintf(argbuf, sizeof(argbuf),
             "fd=%d,offset=%d,len=%d",
             fd, offset, len);

    req.op   = RPC_OP_READ;
    req.path = "/root/nodeA/mem/region1";
    req.args = argbuf;

    rpc_call(g_rpc_transport, &req, &resp, 10000);

    n = (int)resp.data_len;
    if (n > len) n = len;
    if (n > 0 && resp.data)
        memcpy(buf, resp.data, n);

    rpc_free_response(&resp);
    return n;
}

void test_remote_memory_once(void)
{
    uint32_t t0 = rtos_now_time();
    uint32_t t1;
    int fd;
    int d;
    uint8_t buf[128];
    int n, ok = 1;
    int i;

    fd = remote_region_open(128);
    remote_region_write_block(fd, 0, 0xA1, 128);

    n = remote_region_read(fd, 0, buf, 128);
    if (n != 128) ok = 0;
    else {
        for (i = 0; i < 128; i++) {
            if (buf[i] != 0xA1) {
                ok = 0;
                break;
            }
        }
    }

    remote_region_close(fd);

    t1 = rtos_now_time();
    d = t1 - t0;
}

static int vm_migrate_write(void *self, int fd, const void *buf, int len)
{
    (void)self;
    (void)fd;

    const uint8_t *p = buf;

    if (len < 8) {
        printf("STM32: migrate packet too small\n");
        return -1;
    }

    uint32_t img_len = *(uint32_t *)p; p += 4;
    uint32_t ctx_len = *(uint32_t *)p; p += 4;

    if (img_len + ctx_len + 8 > (uint32_t)len) {
        printf("STM32: migrate packet size mismatch\n");
        return -1;
    }

    uint8_t *img = heap_malloc(img_len);
    uint8_t *ctx_buf = heap_malloc(ctx_len);

    if (!img || !ctx_buf) {
        printf("STM32: heap alloc failed\n");
        if (img) heap_free(img);
        if (ctx_buf) heap_free(ctx_buf);
        return -1;
    }

    memcpy(img, p, img_len); p += img_len;
    memcpy(ctx_buf, p, ctx_len);

    printf("STM32: VM migrated in\n");

    struct ccbpf_program *prog = ccbpf_load_from_memory(img, img_len);

    struct ccbpf_ctx ctx;
    if (ccbpf_ctx_unpack(&ctx, ctx_buf, ctx_len) != 0) {
        printf("STM32: ctx_unpack failed\n");
        heap_free(img);
        heap_free(ctx_buf);
        return -1;
    }

    heap_free(ctx_buf);

    unsigned char pkt[1] = {0};

    for (;;) {
        enum ccbpf_status st =
                ccbpf_vm_step(&ctx, prog, pkt, 1, 1, 64);

        if (st == CCBPF_FINISHED) {
            printf("STM32: finished %u\n", ctx.ret);
            break;
        }

        if (st == CCBPF_ERROR) {
            printf("STM32: error\n");
            break;
        }
    }

    heap_free(img);
    return len;
}

static struct file_ops vm_migrate_ops = {
        0,
        0,
        vm_migrate_write,
        0,
        0
};

void vm_migrate_register(void)
{
    world_register("root/nodeB/vm/migrate", &vm_migrate_ops, NULL);
}


void APP(void *ctx)
{
    ccnet_init(NODE_ID_B, NODE_COUNT);

    ccnet_register_node_link(NODE_ID_B, scp_input);
    ccnet_register_node_link(NODE_ID_A, nodeB_provider);

    ccnet_graph_set_edge(NODE_ID_A, NODE_ID_B, 1);
    ccnet_graph_set_edge(NODE_ID_B, NODE_ID_A, 1);

    ccnet_build_routing_table();

    scp_init(4);
    struct scp_stream *ss = scp_stream_alloc(&scp_trans, scp_fd_B, scp_fd_A);

    timer_init();
    timer_create(timer_excu, 10, run);

    rpc_init(16);
    g_rpc_transport = rpc_trans_class_create((void *)rpc_scp_send,
                                             (void *)rpc_scp_recv,
                                             (void *)rpc_scp_close,
                                             NULL);

    sem_process = semaphore_create(0);
    task_create(process_rcv, 512, NULL, 0, 20, &t_process);
    __HAL_UART_CLEAR_OREFLAG(&huart1);

    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    HAL_UART_Receive_IT(&huart1, rcv_buf, 256);

    world_init();
    led_register();
    led1_register();
    vm_migrate_register();

    scp_connect(scp_fd_B);
    while(ss->state != SCP_ESTABLISHED) {}
    HAL_Delay(1000);

    world_sync_node("nodeB", g_rpc_transport);
    //test_remote_memory_once();

    task_delete(t1);
}

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_USART1_UART_Init();

    scheduler_init();
    task_create(APP, 1024, NULL, 0, 100, &t1);
    scheduler_start();

    while (1) {
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM1 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM1) {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
