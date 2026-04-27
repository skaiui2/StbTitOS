#include "rpc_port.h"
#include "mutex.h"
#include "sem.h"
#include "heap.h"
#include "schedule.h"

struct rpc_waiter {
    semaphore_handle sem;
};

static mutex_handle g_rpc_lock = NULL;

static void init_once(void)
{
    static uint8_t inited = 0;
    if (inited) return;
    g_rpc_lock = mutex_create();
    inited = 1;
}

void rpc_port_lock(void)
{
    init_once();
    while (!mutex_lock(g_rpc_lock, 0xFFFFFFFFu))
        ;
}

void rpc_port_unlock(void)
{
    mutex_unlock(g_rpc_lock);
}

struct rpc_waiter *rpc_waiter_create(void)
{
    struct rpc_waiter *w = heap_malloc(sizeof(*w));
    if (!w) return NULL;
    w->sem = semaphore_create(0);
    if (!w->sem) {
        heap_free(w);
        return NULL;
    }
    return w;
}

void rpc_waiter_destroy(struct rpc_waiter *w)
{
    if (!w) return;
    semaphore_delete(w->sem);
    heap_free(w);
}

int rpc_waiter_wait(struct rpc_waiter *w, uint32_t timeout_ms)
{
    if (!w) return -1;
    if (semaphore_take(w->sem, timeout_ms ? timeout_ms : 0) == true)
        return 0;
    return -1;
}

void rpc_waiter_wake(struct rpc_waiter *w)
{
    if (!w) return;
    semaphore_release(w->sem);
}

uint32_t rpc_now_ms(void)
{
    return rtos_now_time();
}
