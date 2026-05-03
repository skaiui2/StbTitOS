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
    uint32_t lock = EnterCritical();
    TaskHandle_t wake = NULL;

    if (sem->wait_tree.count) {
        wake = first_respond_ipc(&sem->wait_tree);
        delay_adt_remove(wake);
        remove_ipc(wake);
    }

    sem->value++;

    ExitCritical(lock);

    if (wake)
        task_adt_add(wake, Ready);

    return true;
}

uint8_t semaphore_take(semaphore_handle sem, uint32_t ticks)
{
    uint32_t lock = EnterCritical();
    TaskHandle_t cur = get_current_tcb();
    uint8_t pend = schedule_PendSV;

    if (sem->value > 0) {
        sem->value--;
        ExitCritical(lock);
        return true;
    }

    if (ticks == 0) {
        ExitCritical(lock);
        return false;
    }

    insert_ipc(cur, &sem->wait_tree);
    ExitCritical(lock);

    if (ticks > 0)
        task_delay(ticks);

    while (pend == schedule_PendSV)
        ;

    lock = EnterCritical();

    if (!check_ipc_state(cur)) {
        remove_ipc(cur);
        ExitCritical(lock);
        return false;
    }

    if (sem->value > 0)
        sem->value--;
    else {
        ExitCritical(lock);
        return false;
    }

    ExitCritical(lock);
    return true;
}
