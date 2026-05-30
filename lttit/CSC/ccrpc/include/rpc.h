#ifndef RPC_H
#define RPC_H

#include <stdint.h>
#include <stddef.h>
#include "link_list.h"
#include "lttit_config.h"

typedef enum {
    RPC_STATUS_OK              = 0,
    RPC_STATUS_INTERNAL_ERROR  = 1,
    RPC_STATUS_TRANSPORT_ERROR = 2,
    RPC_STATUS_TIMEOUT         = 3,
} rpc_status_t;

typedef enum {
    RPC_OP_OPEN  = 1,
    RPC_OP_READ  = 2,
    RPC_OP_WRITE = 3,
    RPC_OP_CTL   = 4,
    RPC_OP_CLOSE = 5,
} rpc_op_t;

#define TLV_OP        0x01
#define TLV_PATH      0x02
#define TLV_ARGS      0x03
#define TLV_DATA      0x04
#define TLV_OUTPUT    0x10
#define TLV_EXITCODE  0x11


struct rpc_buf {
    struct list_node node;
    size_t len;
    uint8_t data[];
};

struct rpc_reasm {
    struct list_node head;
    size_t total_len;
};

struct rpc_header {
    uint16_t status;
    uint32_t seq;
    uint32_t msg_len;
} __attribute__((packed));

struct rpc_message {
    struct rpc_header hdr;
    uint8_t payload[];
};

struct rpc_transport_class {
    size_t (*send)(void *user, const uint8_t *buf, size_t len);
    size_t (*recv)(void *user, uint8_t *buf, size_t maxlen);
    void   (*close)(void *user);
    void *user;
    struct rpc_reasm reasm;
};

struct rpc_request {
    uint32_t op;
    char    *path;
    char    *args;
    uint8_t *data;    
    uint32_t data_len;
};

struct rpc_response {
    char    *output;
    uint8_t *data;
    uint32_t data_len;
    uint32_t exitcode;
};

typedef int (*rpc_handler_t)(
    const struct rpc_request  *in,
    struct rpc_response       *out
);

void rpc_port_set_current_request(const struct rpc_request *r);
const struct rpc_request *rpc_port_get_current_request(void);
void rpc_port_set_current_response(struct rpc_response *r);
struct rpc_response *rpc_port_get_current_response(void);

void rpc_init(uint8_t pending_cap);

struct rpc_transport_class *rpc_trans_class_create(
    void *send, void *recv, void *close, void *user);

void rpc_set_handler(rpc_handler_t h);

int rpc_call(struct rpc_transport_class *t,
             const struct rpc_request *in,
             struct rpc_response *out,
             uint32_t timeout_ms);

void rpc_on_data(struct rpc_transport_class *t,
                 const uint8_t *buf, size_t len);

void rpc_free_request(struct rpc_request *r);
void rpc_free_response(struct rpc_response *r);

#endif
