#include "mutex.h"
#include "heap.h"
#include "schedule.h"
#include "rbtree.h"

struct mutex {
    uint8_t value;
    struct rb_root wait_tree;
    TaskHandle_t owner;
    uint32_t owner_orig_prio;
};

static void inherit_prio(TaskHandle_t owner, TaskHandle_t waiter)
{
    if (!owner)
        return;

    uint32_t op = task_get_sched_prio(owner);
    uint32_t wp = task_get_sched_prio(waiter);

    if (wp < op)
        rtos_task_change_prio(owner, wp);

    void *obj = rtos_task_get_waiting_obj(owner);
    if (obj) {
        struct mutex *m = obj;
        inherit_prio(m->owner, owner);
    }
}

mutex_handle mutex_create(void)
{
    struct mutex *m = heap_malloc(sizeof(*m));
    if (!m)
        return NULL;

    m->value = 1;
    m->owner = NULL;
    m->owner_orig_prio = 0;
    rb_root_init(&m->wait_tree);
    return m;
}

void mutex_delete(mutex_handle m)
{
    heap_free(m);
}

uint8_t mutex_lock(mutex_handle m, uint32_t ticks)
{
    TaskHandle_t cur = get_current_tcb();
    uint8_t pend = schedule_PendSV;

    scheduler_lock();

    if (m->value > 0) {
        m->value--;
        m->owner = cur;
        m->owner_orig_prio = task_get_sched_prio(cur);
        rtos_task_set_waiting_obj(cur, NULL);
        scheduler_unlock();
        return true;
    }

    if (ticks == 0) {
        scheduler_unlock();
        return false;
    }

    rtos_task_set_waiting_obj(cur, m);
    insert_ipc(cur, &m->wait_tree);
    inherit_prio(m->owner, cur);

    scheduler_unlock();

    if (ticks > 0)
        task_delay(ticks);

    while (pend == schedule_PendSV)
        ;

    scheduler_lock();

    if (!check_ipc_state(cur)) {
        remove_ipc(cur);
        rtos_task_set_waiting_obj(cur, NULL);
        scheduler_unlock();
        return false;
    }

    m->value--;
    m->owner = cur;
    m->owner_orig_prio = task_get_sched_prio(cur);
    rtos_task_set_waiting_obj(cur, NULL);

    scheduler_unlock();
    return true;
}

uint8_t mutex_unlock(mutex_handle m)
{
    TaskHandle_t cur = get_current_tcb();
    TaskHandle_t wake = NULL;

    scheduler_lock();

    if (m->wait_tree.count) {
        wake = first_respond_ipc(&m->wait_tree);
        delay_adt_remove(wake);
        remove_ipc(wake);
        rtos_task_set_waiting_obj(wake, NULL);

        if (sched_should_preempt(wake, cur))
            scheduler_request_switch();
    }

    if (m->owner)
        rtos_task_change_prio(m->owner, m->owner_orig_prio);

    m->value++;
    m->owner = NULL;

    scheduler_unlock();

    if (wake)
        task_adt_add(wake, Ready);

    return true;
}
