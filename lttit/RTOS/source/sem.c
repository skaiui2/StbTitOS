#include "sem.h"
#include "heap.h"
#include "schedule.h"
#include "rbtree.h"

struct semaphore {
    uint8_t value;
    struct rb_root wait_tree;
};

semaphore_handle semaphore_create(uint8_t value)
{
    struct semaphore *sem = heap_malloc(sizeof(*sem));
    if (!sem)
        return NULL;

    sem->value = value;
    rb_root_init(&sem->wait_tree);
    return sem;
}

void semaphore_delete(semaphore_handle sem)
{
    heap_free(sem);
}

uint8_t semaphore_release(semaphore_handle sem)
{
    TaskHandle_t cur = get_current_tcb();
    TaskHandle_t wake = NULL;

    scheduler_lock();

    if (sem->wait_tree.count) {
        wake = first_respond_ipc(&sem->wait_tree);
        delay_adt_remove(wake);
        remove_ipc(wake);
    }

    sem->value++;

    if (wake && sched_should_preempt(wake, cur))
        scheduler_request_switch();

    scheduler_unlock();

    if (wake)
        task_adt_add(wake, Ready);

    return true;
}

uint8_t semaphore_take(semaphore_handle sem, uint32_t ticks)
{
    TaskHandle_t cur = get_current_tcb();
    uint8_t pend = schedule_PendSV;

    scheduler_lock();

    if (sem->value > 0) {
        sem->value--;
        scheduler_unlock();
        return true;
    }

    if (ticks == 0) {
        scheduler_unlock();
        return false;
    }

    insert_ipc(cur, &sem->wait_tree);

    scheduler_unlock();

    if (ticks > 0)
        task_delay(ticks);

    while (pend == schedule_PendSV)
        ;

    scheduler_lock();

    if (!check_ipc_state(cur)) {
        remove_ipc(cur);
        scheduler_unlock();
        return false;
    }

    if (sem->value > 0)
        sem->value--;
    else {
        scheduler_unlock();
        return false;
    }

    scheduler_unlock();
    return true;
}
