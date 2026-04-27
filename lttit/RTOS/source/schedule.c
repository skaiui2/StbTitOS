#include "schedule.h"
#include "heap.h"
#include "port.h"
#include "hashmap.h"
#include "rbtree.h"
#include "atomic.h"
#include "macro.h"
#include "compare.h"
#include "timer.h"
#include <stdio.h>

#define SCHED_DEBUG 0
#if SCHED_DEBUG
#define sched_log(fmt, ...) printf(fmt, ##__VA_ARGS__)
#else
#define sched_log(fmt, ...) do {} while (0)
#endif

static spinlock_t sched_lock;
volatile uint32_t preempt_count;
volatile uint8_t need_resched;

static struct rb_root ReadyRTTree;
static struct rb_root WakeTicksTree;
static struct rb_root SuspendTree;
static struct rb_root DeleteTree;
static struct hashmap pid_map;

volatile uint32_t NowTickCount = 0;
static uint32_t next_pid = 1;

struct TCB_t {
    volatile uint32_t *pxTopOfStack;
    uint32_t pid;
    struct rb_node task_node;
    struct rb_node IPC_node;
    struct rb_node delay_node;
    uint16_t period;
    uint16_t deadline;
    uint32_t EnterTime;
    uint32_t ExitTime;
    uint32_t SmoothTime;
    uint32_t abs_deadline;
    uint32_t stack_mem;
    uint32_t max_used_mem;
    uint32_t *pxStack;
    uint32_t miss_count;
    void *waiting_obj;
};

__attribute__((used)) TaskHandle_t volatile schedule_currentTCB;
TaskHandle_t leisureTcb;
uint32_t leisureCount;
uint8_t volatile schedule_PendSV;

uint8_t task_is_rt(TaskHandle_t t)
{
    return t->deadline != 0;
}

uint32_t rtos_now_time(void)
{
    return NowTickCount;
}

uint32_t task_get_sched_prio(TaskHandle_t t)
{
    return t->abs_deadline;
}

void task_set_sched_prio(TaskHandle_t t, uint32_t prio)
{
    t->abs_deadline = prio;
}

static inline uint32_t rt_prio_value(TaskHandle_t t)
{
    int32_t diff = (int32_t)(t->abs_deadline - NowTickCount);
    if (diff <= 0)
        return 0;
    return (uint32_t)diff;
}

static inline uint32_t ipc_prio_value(TaskHandle_t t)
{
    return t->abs_deadline;
}

TaskHandle_t get_current_tcb(void)
{
    return schedule_currentTCB;
}

void rtos_task_set_waiting_obj(TaskHandle_t t, void *obj)
{
    t->waiting_obj = obj;
}

void *rtos_task_get_waiting_obj(TaskHandle_t t)
{
    return t->waiting_obj;
}

static void rt_ready_add(TaskHandle_t t)
{
    struct rb_node *node = &t->task_node;

    node->root  = &ReadyRTTree;
    node->value = rt_prio_value(t);
    rb_insert_node(&ReadyRTTree, node);
    sched_log("[READY-RT] pid=%lu prio=%lu\n", t->pid, node->value);
}

static void rt_ready_remove(TaskHandle_t t)
{
    rb_remove_node(&ReadyRTTree, &t->task_node);
    sched_log("[REMOVE-RT] pid=%lu\n", t->pid);
}

static TaskHandle_t rt_ready_pick_first(void)
{
    struct rb_node *n = ReadyRTTree.first_node;

    if (!n)
        return NULL;

    return container_of(n, struct TCB_t, task_node);
}

TaskHandle_t first_respond_ipc(rb_root_handle root)
{
    struct rb_node *n = root->first_node;

    if (!n)
        return NULL;

    return container_of(n, struct TCB_t, IPC_node);
}

void insert_ipc(TaskHandle_t self, struct rb_root *root)
{
    uint32_t prio = ipc_prio_value(self);

    self->IPC_node.root  = root;
    self->IPC_node.value = prio;
    rb_insert_node(root, &self->IPC_node);
    sched_log("[IPC-ADD] pid=%lu prio=%lu\n", self->pid, prio);
}

void remove_ipc(TaskHandle_t self)
{
    if (self->IPC_node.root) {
        rb_remove_node(self->IPC_node.root, &self->IPC_node);
        self->IPC_node.root = NULL;
        sched_log("[IPC-DEL] pid=%lu\n", self->pid);
    }
}

uint8_t check_ipc_state(TaskHandle_t taskHandle)
{
    return taskHandle->IPC_node.root == NULL;
}

void scheduler_lock(void)
{
    preempt_disable();
}

void scheduler_unlock(void)
{
    preempt_enable();
    if (preempt_count == 0 && need_resched) {
        need_resched = 0;
        schedule();
    }
}

void scheduler_request_switch(void)
{
    if (preempt_count == 0) {
        schedule();
    } else {
        need_resched = 1;
    }
}

void adt_init(void)
{
    rb_root_init(&ReadyRTTree);
    rb_root_init(&SuspendTree);
    rb_root_init(&DeleteTree);
}

void task_adt_add(TaskHandle_t self, uint8_t state)
{
    scheduler_lock();

    if (state == Ready) {
        rt_ready_add(self);
    } else {
        self->task_node.root = &SuspendTree;
        rb_insert_node(&SuspendTree, &self->task_node);
    }

    scheduler_unlock();
}

void task_adt_remove(TaskHandle_t self, uint8_t state)
{
    scheduler_lock();

    if (state == Ready) {
        rt_ready_remove(self);
    } else {
        rb_remove_node(&SuspendTree, &self->task_node);
    }

    scheduler_unlock();
}

void tree_delay_init(void)
{
    rb_root_init(&WakeTicksTree);
}

void record_wake_time(uint16_t ticks)
{
    scheduler_lock();
    TaskHandle_t self = schedule_currentTCB;

    self->delay_node.value = NowTickCount + ticks;
    self->delay_node.root  = &WakeTicksTree;
    rb_insert_node(&WakeTicksTree, &self->delay_node);

    scheduler_unlock();
}

void delay_adt_remove(TaskHandle_t self)
{
    rb_remove_node(&WakeTicksTree, &self->delay_node);
}

void task_delay(uint16_t ticks)
{
    if (ticks) {
        task_adt_remove(schedule_currentTCB, Ready);
        record_wake_time(ticks);
        scheduler_request_switch();
    }
}

static uint32_t task_pid_alloc(void)
{
    return next_pid++;
}

uint32_t task_create(TaskFunction_t task_code,
                     uint16_t stack_depth,
                     void *parameters,
                     uint16_t period,
                     uint16_t deadline,
                     TaskHandle_t *self)
{
    uint32_t *top_stack;
    uint32_t *px_stack;
    struct TCB_t *new_tcb;
    size_t stack_mem = (size_t)stack_depth * sizeof(uintptr_t);

    px_stack = (uint32_t *)heap_malloc(stack_mem);
    new_tcb  = heap_malloc(sizeof(*new_tcb));

    *self = new_tcb;
    *new_tcb = (struct TCB_t){
        .period       = period,
        .deadline     = deadline,
        .SmoothTime   = 0,
        .abs_deadline = 0,
        .stack_mem    = stack_mem,
        .pxStack      = px_stack,
        .pid          = task_pid_alloc(),
        .waiting_obj  = NULL,
    };

    if (deadline != 0)
        new_tcb->abs_deadline = NowTickCount + deadline;
    else
        new_tcb->abs_deadline = 0xFFFFFFFFu;

    hashmap_put(&pid_map, (void *)(uintptr_t)new_tcb->pid, new_tcb);

    top_stack = new_tcb->pxStack + (stack_depth - 1);
    top_stack = (uint32_t *)((uint32_t)top_stack & ~(uint32_t)alignment_byte);
    new_tcb->pxTopOfStack = StackInit(top_stack, task_code, parameters);

    rb_node_init(&new_tcb->task_node);
    rb_node_init(&new_tcb->IPC_node);
    rb_node_init(&new_tcb->delay_node);

    sched_log("[CREATE] pid=%lu deadline=%u period=%u\n",
              new_tcb->pid, new_tcb->deadline, new_tcb->period);

    if (new_tcb->pid != 1)
        task_adt_add(new_tcb, Ready);

    return new_tcb->pid;
}

void task_delete(TaskHandle_t self)
{
    scheduler_lock();

    sched_log("[DELETE] pid=%lu\n", self->pid);

    hashmap_remove(&pid_map, (void *)(uintptr_t)self->pid);
    task_adt_remove(self, Ready);

    self->task_node.root = &DeleteTree;
    rb_insert_node(&DeleteTree, &self->task_node);

    scheduler_unlock();
    scheduler_request_switch();
}

void task_free(void)
{
    scheduler_lock();

    if (DeleteTree.count) {
        struct rb_node *n = rb_last(&DeleteTree);
        TaskHandle_t self =
                container_of(n, struct TCB_t, task_node);

        rb_remove_node(&DeleteTree, &self->task_node);
        scheduler_unlock();

        sched_log("[FREE] pid=%lu\n", self->pid);

        heap_free(self->pxStack);
        heap_free(self);
    } else {
        scheduler_unlock();
    }
}

uint32_t task_enter(void)
{
    struct TCB_t *self = schedule_currentTCB;

    self->EnterTime = NowTickCount;
    sched_log("[ENTER] pid=%lu time=%lu\n", self->pid, self->EnterTime);
    return self->EnterTime;
}

uint32_t task_exit(void)
{
    struct TCB_t *self = schedule_currentTCB;
    uint32_t new_period;

    self->ExitTime = NowTickCount;
    new_period = self->ExitTime - self->EnterTime;

    if (self->SmoothTime != 0) {
        uint32_t old = self->SmoothTime;

        self->SmoothTime = (old * 7 + new_period) >> 3;
    } else {
        self->SmoothTime = new_period;
    }

    if (self->deadline && NowTickCount > self->abs_deadline)
        self->miss_count++;

    sched_log("[EXIT] pid=%lu exec=%lu smooth=%lu\n",
              self->pid, new_period, self->SmoothTime);

    task_delay(self->period);

    return new_period;
}

void leisure_task(void)
{
    for (;;) {
        leisureCount++;
        task_free();
    }
}

void leisure_task_create(void)
{
    task_create((TaskFunction_t)leisure_task,
                64,
                NULL,
                0,
                0,
                &leisureTcb);
}

void rtos_stack_used(TaskHandle_t tcb)
{
    uint8_t *stack_end = (uint8_t *)tcb->pxStack + tcb->stack_mem;
    size_t used = (size_t)(stack_end - (uint8_t *)tcb->pxTopOfStack);

    if (used > tcb->max_used_mem)
        tcb->max_used_mem = used;
}

void task_switch_context(void)
{
    TaskHandle_t old = schedule_currentTCB;
    TaskHandle_t next = NULL;

    schedule_PendSV++;

    if (schedule_currentTCB)
        rtos_stack_used(schedule_currentTCB);

    if (ReadyRTTree.count > 0)
        next = rt_ready_pick_first();
    else
        next = leisureTcb;

    schedule_currentTCB = next;

    sched_log("[CTX] %lu -> %lu\n",
              old ? old->pid : 0,
              schedule_currentTCB ? schedule_currentTCB->pid : 0);
}

void scheduler_init(void)
{
    spinlock_init(&sched_lock);
    preempt_count = 0;
    need_resched = 0;
    adt_init();
    hashmap_init(&pid_map, TASK_COUNT, HASHMAP_KEY_INT);
    tree_delay_init();
    leisure_task_create();
}

void scheduler_start(void)
{
    task_switch_context();
    StartFirstTask();
}

void check_ticks(void)
{
    struct rb_node *n;

    NowTickCount++;

    while ((n = WakeTicksTree.first_node) &&
           compare_before_eq(n->value, NowTickCount)) {

        TaskHandle_t self =
                container_of(n, struct TCB_t, delay_node);

        delay_adt_remove(self);

        if (self->deadline != 0 && self->period != 0)
            self->abs_deadline = NowTickCount + self->deadline;

        task_adt_add(self, Ready);

        sched_log("[WAKE] pid=%lu at tick=%lu\n", self->pid, NowTickCount);

        if (schedule_currentTCB == leisureTcb) {
            scheduler_request_switch();
        } else if (task_is_rt(self) && !task_is_rt(schedule_currentTCB)) {
            scheduler_request_switch();
        } else if (task_is_rt(self) && task_is_rt(schedule_currentTCB)) {
            if (self->abs_deadline < schedule_currentTCB->abs_deadline)
                scheduler_request_switch();
        }
    }
}

int sched_should_preempt(TaskHandle_t new_task, TaskHandle_t cur_task)
{
    if (cur_task == leisureTcb)
        return 1;

    if (new_task->abs_deadline < cur_task->abs_deadline)
        return 1;

    return 0;
}

uint8_t rtos_task_state(TaskHandle_t tcb)
{
    if (tcb == schedule_currentTCB)
        return RUNNING;

    if (tcb == leisureTcb ||
        tcb->task_node.root == &ReadyRTTree)
        return Ready;

    if (tcb->delay_node.root == &WakeTicksTree)
        return OS_Delay;

    if (tcb->task_node.root == &SuspendTree)
        return Suspend;

    if (tcb->task_node.root == &DeleteTree)
        return Dead;

    return Suspend;
}

int rtos_get_task_info(uint32_t pid, struct task_info *out)
{
    scheduler_lock();
    TaskHandle_t tcb = hashmap_get(&pid_map, (void *)(uintptr_t)pid);

    if (!tcb) {
        scheduler_unlock();
        return -1;
    }

    out->pid             = tcb->pid;
    out->stack_watermark = tcb->max_used_mem;
    out->period          = tcb->period;
    out->deadline        = tcb->deadline;
    out->state           = rtos_task_state(tcb);

    scheduler_unlock();
    return 0;
}

void rtos_task_change_prio(TaskHandle_t t, uint32_t new_prio)
{
    uint8_t state;

    scheduler_lock();

    state = rtos_task_state(t);

    if (state == Ready) {
        rt_ready_remove(t);
    }

    task_set_sched_prio(t, new_prio);

    if (state == Ready) {
        rt_ready_add(t);
    }

    scheduler_unlock();
}
