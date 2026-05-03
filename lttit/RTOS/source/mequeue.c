#include <string.h>
#include "mequeue.h"
#include "heap.h"
#include "schedule.h"
#include "rbtree.h"
#include "port.h"

struct queue_struct {
    uint8_t *start;
    uint8_t *end;
    uint8_t *read;
    uint8_t *write;
    uint8_t msg_num;
    struct rb_root send_tree;
    struct rb_root recv_tree;
    uint32_t node_size;
    uint32_t node_num;
};

static TaskHandle_t write_to_queue(struct queue_struct *q, uint32_t *buf)
{
    memcpy(q->write, buf, q->node_size);
    q->write += q->node_size;

    if (q->write >= q->end)
        q->write = q->start;

    q->msg_num++;

    if (q->recv_tree.count)
        return first_respond_ipc(&q->recv_tree);

    return NULL;
}

static TaskHandle_t extract_from_queue(struct queue_struct *q, uint32_t *buf)
{
    q->read += q->node_size;

    if (q->read >= q->end)
        q->read = q->start;

    memcpy(buf, q->read, q->node_size);

    q->msg_num--;

    if (q->send_tree.count)
        return first_respond_ipc(&q->send_tree);

    return NULL;
}

struct queue_struct *queue_create(uint32_t len, uint32_t size)
{
    size_t qsize = len * size;
    struct queue_struct *q = heap_malloc(sizeof(*q) + qsize);
    if (!q)
        return NULL;

    uint8_t *msg_start = (uint8_t *)q + sizeof(*q);

    *q = (struct queue_struct){
            .start     = msg_start,
            .end       = msg_start + qsize,
            .read      = msg_start + (len - 1) * size,
            .write     = msg_start,
            .msg_num   = 0,
            .node_num  = len,
            .node_size = size,
    };

    rb_root_init(&q->send_tree);
    rb_root_init(&q->recv_tree);

    return q;
}

void queue_delete(struct queue_struct *q)
{
    heap_free(q);
}

uint8_t queue_send(struct queue_struct *q, uint32_t *buf, uint32_t ticks)
{
    uint32_t lock = EnterCritical();

    TaskHandle_t cur = get_current_tcb();
    uint8_t pend = schedule_PendSV;

    if (q->msg_num < q->node_num) {
        TaskHandle_t wake = write_to_queue(q, buf);

        if (wake) {
            delay_adt_remove(wake);
            remove_ipc(wake);
        }

        ExitCritical(lock);

        if (wake) {
            task_adt_add(wake, Ready);
        }

        return true;
    }

    if (ticks == 0) {
        ExitCritical(lock);
        return false;
    }

    insert_ipc(cur, &q->send_tree);
    ExitCritical(lock);

    task_delay(ticks);

    while (pend == schedule_PendSV)
        ;

    lock = EnterCritical();

    if (!check_ipc_state(cur)) {
        ExitCritical(lock);
        return false;
    }

    remove_ipc(cur);
    TaskHandle_t wake = write_to_queue(q, buf);

    if (wake) {
        delay_adt_remove(wake);
        remove_ipc(wake);
    }

    ExitCritical(lock);

    if (wake) {
        task_adt_add(wake, Ready);
    }

    return true;
}

uint8_t queue_receive(struct queue_struct *q, uint32_t *buf, uint32_t ticks)
{
    uint32_t lock = EnterCritical();

    TaskHandle_t cur = get_current_tcb();
    uint8_t pend = schedule_PendSV;

    if (q->msg_num > 0) {
        TaskHandle_t wake = extract_from_queue(q, buf);

        if (wake) {
            delay_adt_remove(wake);
            remove_ipc(wake);
        }

        ExitCritical(lock);

        if (wake) {
            task_adt_add(wake, Ready);
        }

        return true;
    }

    if (ticks == 0) {
        ExitCritical(lock);
        return false;
    }

    insert_ipc(cur, &q->recv_tree);
    ExitCritical(lock);

    task_delay(ticks);

    while (pend == schedule_PendSV)
        ;

    lock = EnterCritical();

    if (!check_ipc_state(cur)) {
        remove_ipc(cur);
        ExitCritical(lock);
        return false;
    }

    remove_ipc(cur);
    TaskHandle_t wake = extract_from_queue(q, buf);

    if (wake) {
        delay_adt_remove(wake);
        remove_ipc(wake);
    }

    ExitCritical(lock);

    if (wake) {
        task_adt_add(wake, Ready);
    }

    return true;
}
