#include "timer.h"
#include "heap.h"
#include "compare.h"
#include "port.h"
#include "macro.h"
#include "rbtree.h"

extern uint32_t NowTickCount;

struct timer_obj {
    struct rb_node node;
    uint32_t period;
    TimerFunction_t callback;
    uint8_t stop_flag;
};

static struct rb_root clock_tree;

void timer_init(void)
{
    rb_root_init(&clock_tree);
}

static void clock_tree_add(struct timer_obj *t)
{
    uint32_t key = EnterCritical();
    t->node.value = NowTickCount + t->period;
    rb_insert_node(&clock_tree, &t->node);
    ExitCritical(key);
}

static void clock_tree_remove(struct timer_obj *t)
{
    uint32_t key = EnterCritical();
    rb_remove_node(&clock_tree, &t->node);
    ExitCritical(key);
}

TimerHandle timer_create(TimerFunction_t cb,
                         uint32_t period,
                         uint8_t flag)
{
    struct timer_obj *t = heap_malloc(sizeof(*t));
    if (!t)
        return NULL;

    *t = (struct timer_obj){
            .period = period,
            .callback = cb,
            .stop_flag = flag,
    };

    rb_node_init(&t->node);
    clock_tree_add(t);

    return t;
}

void timer_delete(TimerHandle t)
{
    clock_tree_remove(t);
    heap_free(t);
}

void timer_tick(void)
{
    uint32_t key = EnterCritical();

    struct rb_node *n;

    while ((n = clock_tree.first_node) &&
           compare_before_eq(n->value, NowTickCount)) {

        struct timer_obj *t =
                container_of(n, struct timer_obj, node);

        t->callback(t);

        clock_tree_remove(t);

        if (t->stop_flag == run)
            clock_tree_add(t);
    }

    ExitCritical(key);
}
