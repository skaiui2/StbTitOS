#ifndef RPC_PORT_H
#define RPC_PORT_H

#include <stdint.h>

struct rpc_waiter;

struct rpc_waiter *rpc_waiter_create(void);
void rpc_waiter_destroy(struct rpc_waiter *w);

int rpc_waiter_wait(struct rpc_waiter *w, uint32_t timeout_ms);
void rpc_waiter_wake(struct rpc_waiter *w);

uint32_t rpc_now_ms(void);

void rpc_port_lock(void);
void rpc_port_unlock(void);

#endif
