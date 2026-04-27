#ifndef SCHEDULE_H
#define SCHEDULE_H

#include <stddef.h>
#include <stdint.h>
#include "rbtree.h"

/*
 * For kernel port!
*/

#define true    1
#define false   0

#define Ready       0
#define Suspend     1
#define Dead        2
#define OS_Delay    3
#define RUNNING     4

#define TASK_COUNT 10

typedef void (*TaskFunction_t)(void *);
typedef struct TCB_t *TaskHandle_t;
extern volatile uint8_t schedule_PendSV;
TaskHandle_t get_current_tcb(void);

void rtos_task_set_waiting_obj(TaskHandle_t t, void *obj);
void *rtos_task_get_waiting_obj(TaskHandle_t t);

uint32_t task_create(TaskFunction_t task_code,
                     uint16_t stack_depth,
                     void *parameters,
                     uint16_t period,
                     uint16_t deadline,
                     TaskHandle_t *self);

void task_delete(TaskHandle_t self);
void task_delay(uint16_t ticks);

uint32_t task_get_sched_prio(TaskHandle_t t);
void task_set_sched_prio(TaskHandle_t t, uint32_t prio);

void task_adt_add(TaskHandle_t self, uint8_t state);
void task_adt_remove(TaskHandle_t self, uint8_t state);
void delay_adt_remove(TaskHandle_t self);

void insert_ipc(TaskHandle_t self, rb_root_handle root);
void remove_ipc(TaskHandle_t self);
TaskHandle_t first_respond_ipc(rb_root_handle root);

void scheduler_init(void);
void scheduler_start(void);

uint8_t check_ipc_state(TaskHandle_t task_handle);

TaskHandle_t get_current_tcb(void);

void adt_init(void);
void tree_delay_init(void);
void record_wake_time(uint16_t ticks);
void check_ticks(void);

void scheduler_lock(void);
void scheduler_unlock(void);
void scheduler_request_switch(void);

int sched_should_preempt(TaskHandle_t new_task, TaskHandle_t cur_task);


/*
 * For user port!
*/
uint32_t task_enter(void);
uint32_t task_exit(void);

struct task_info {
    uint32_t pid;
    uint32_t stack_watermark;
    uint32_t period;
    uint32_t deadline;
    uint8_t  state;
};

int rtos_get_task_info(uint32_t pid, struct task_info *out);
void rtos_task_change_prio(TaskHandle_t t, uint32_t new_prio);
uint32_t rtos_now_time(void);

#endif
