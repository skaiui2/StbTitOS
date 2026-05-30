#include <string.h>
#include <stdio.h>
#include "hashmap.h"
#include "common.h"
#include "rpc.h"
#include "lttit_config.h"
#include "rpc_port.h"
#include "heap.h"

struct pending_call {
    uint32_t            seq;
    struct rpc_response *out;   
    rpc_status_t        status;
    struct rpc_waiter  *waiter;
};

static uint32_t g_next_seq = 1;
static struct hashmap g_pending;
static rpc_handler_t g_handler = NULL;
static uint8_t poll_buf[RPC_BUF_DEFAULT];
static const struct rpc_request  *g_current_rpc_req  = NULL;
static struct rpc_response       *g_current_rpc_resp = NULL;

void rpc_port_set_current_request(const struct rpc_request *r)
{
    g_current_rpc_req = r;
}

const struct rpc_request *rpc_port_get_current_request(void)
{
    return g_current_rpc_req;
}

void rpc_port_set_current_response(struct rpc_response *r)
{
    g_current_rpc_resp = r;
}

struct rpc_response *rpc_port_get_current_response(void)
{
    return g_current_rpc_resp;
}

static void ccnet_debug_hex(const char *tag, const void *buf, size_t len)
{
#if RPC_DEBUG
    const uint8_t *p = buf;

    printf("---- %s (%zu bytes) ----\n", tag, len);

    for (size_t i = 0; i < len; i++) {
        printf("%02X ", p[i]);
        if ((i + 1) % 16 == 0)
            printf("\n");
    }
    if (len % 16 != 0)
        printf("\n");

    printf("-----------------------------\n");
#endif
}

static int rpc_tx_req_id = 0;

void rpc_debug_dump_tx_request(const char *name,
                               uint32_t seq,
                               const void *buf, size_t len)
{
#if RPC_DEBUG
    printf("\n[RPC TX] REQUEST name=%s seq=%u id:%d\n",
           name, seq, rpc_tx_req_id++);
#endif
}

static int rpc_tx_resp_id = 0;

void rpc_debug_dump_tx_response(uint32_t seq,
                                uint16_t status,
                                const void *buf, size_t len)
{
#if RPC_DEBUG
    printf("\n[RPC TX] RESPONSE seq=%u status=%u id:%d\n",
           seq, status, rpc_tx_resp_id++);
    ccnet_debug_hex("RPC Response", buf, len);
#endif
}

static int rpc_rx_id = 0;
void rpc_debug_dump_rx(const void *buf, size_t len)
{
#if RPC_DEBUG
    printf("\n[RPC RX] id:%d\n", rpc_rx_id++);
    ccnet_debug_hex("RPC RX", buf, len);
#endif
}

static size_t tlv_write_u32(uint8_t *buf, uint8_t type, uint32_t v)
{
    if (buf) {
        buf[0] = type;
        buf[1] = 4;
        buf[2] = 0;
        memcpy(&buf[3], &v, 4);
    }
    return 1 + 2 + 4;
}

static size_t tlv_write_string(uint8_t *buf, uint8_t type, const char *s)
{
    size_t len = s ? strlen(s) : 0;

    if (buf) {
        buf[0] = type;
        buf[1] = (uint8_t)(len & 0xFF);
        buf[2] = (uint8_t)((len >> 8) & 0xFF);
        if (len > 0 && s)
            memcpy(&buf[3], s, len);
    }

    return 1 + 2 + len;
}

static size_t tlv_write_bytes(uint8_t *buf, uint8_t type,
                              const uint8_t *p, uint16_t len)
{
    if (buf) {
        buf[0] = type;
        buf[1] = (uint8_t)(len & 0xFF);
        buf[2] = (uint8_t)((len >> 8) & 0xFF);
        if (len > 0 && p)
            memcpy(&buf[3], p, len);
    }
    return 1 + 2 + len;
}

static size_t tlv_read(const uint8_t *buf, size_t buf_len,
                       uint8_t *type, const uint8_t **value, uint16_t *len)
{
    if (buf_len < 3) return 0;
    *type = buf[0];
    *len  = (uint16_t)(buf[1] | (buf[2] << 8));
    if (buf_len < 3 + *len) return 0;
    *value = &buf[3];
    return 3 + *len;
}

static size_t tlv_read_u32_typed(const uint8_t *buf, size_t buf_len,
                                 uint8_t expect_type, uint32_t *out)
{
    uint8_t type;
    const uint8_t *value;
    uint16_t len;
    size_t used = tlv_read(buf, buf_len, &type, &value, &len);
    if (used == 0 || type != expect_type || len != 4)
        return 0;
    memcpy(out, value, 4);
    return used;
}

static size_t tlv_read_string_typed(const uint8_t *buf, size_t buf_len,
                                    uint8_t expect_type, char **out)
{
    uint8_t type;
    const uint8_t *value;
    uint16_t len;
    size_t used = tlv_read(buf, buf_len, &type, &value, &len);
    if (used == 0 || type != expect_type)
        return 0;
    char *s = heap_malloc(len + 1);
    if (!s) return 0;
    memcpy(s, value, len);
    s[len] = '\0';
    *out = s;
    return used;
}

static size_t encode_request_tlv(uint8_t *buf, const struct rpc_request *in)
{
    size_t off = 0;
    off += tlv_write_u32(buf ? buf + off : NULL, TLV_OP, in->op);
    off += tlv_write_string(buf ? buf + off : NULL,
                            TLV_PATH, in->path ? in->path : "");
    off += tlv_write_string(buf ? buf + off : NULL,
                            TLV_ARGS, in->args ? in->args : "");
    if (in->data && in->data_len > 0 && in->data_len <= 0xFFFF)
        off += tlv_write_bytes(buf ? buf + off : NULL, TLV_DATA,
                               in->data, (uint16_t)in->data_len);
    return off;
}

static size_t encode_response_tlv(uint8_t *buf, const struct rpc_response *in)
{
    size_t off = 0;
    off += tlv_write_string(buf ? buf + off : NULL,
                            TLV_OUTPUT, in->output ? in->output : "");
    off += tlv_write_u32(buf ? buf + off : NULL,
                         TLV_EXITCODE, in->exitcode);
    if (in->data && in->data_len > 0 && in->data_len <= 0xFFFF)
        off += tlv_write_bytes(buf ? buf + off : NULL, TLV_DATA,
                               in->data, (uint16_t)in->data_len);
    return off;
}

static int decode_request_tlv(const uint8_t *buf, size_t len,
                              struct rpc_request *out)
{
    memset(out, 0, sizeof(*out));
    size_t used = 0;
    while (used < len) {
        uint8_t type;
        const uint8_t *value;
        uint16_t vlen;
        size_t n = tlv_read(buf + used, len - used, &type, &value, &vlen);
        if (n == 0) return -1;

        switch (type) {
        case TLV_OP:
            if (vlen == 4) memcpy(&out->op, value, 4);
            break;
        case TLV_PATH: {
            char *s = heap_malloc(vlen + 1);
            if (!s) return -1;
            memcpy(s, value, vlen);
            s[vlen] = 0;
            out->path = s;
            break;
        }
        case TLV_ARGS: {
            char *s = heap_malloc(vlen + 1);
            if (!s) return -1;
            memcpy(s, value, vlen);
            s[vlen] = 0;
            out->args = s;
            break;
        }

        case TLV_DATA: {
            uint8_t *p = heap_malloc(vlen);
            if (!p) return -1;
            memcpy(p, value, vlen);
            out->data     = p;
            out->data_len = vlen;
            break;
        }
        default:
            break;
        }

        used += n;
    }

    return 0;
}

static int decode_response_tlv(const uint8_t *buf, size_t len,
                               struct rpc_response *out)
{
    memset(out, 0, sizeof(*out));

    size_t used = 0;
    while (used < len) {
        uint8_t type;
        const uint8_t *value;
        uint16_t vlen;
        size_t n = tlv_read(buf + used, len - used, &type, &value, &vlen);
        if (n == 0) return -1;

        switch (type) {
        case TLV_OUTPUT: {
            char *s = heap_malloc(vlen + 1);
            if (!s) return -1;
            memcpy(s, value, vlen);
            s[vlen] = 0;
            out->output = s;
            break;
        }
        case TLV_EXITCODE:
            if (vlen == 4) memcpy(&out->exitcode, value, 4);
            break;
        case TLV_DATA: {
            uint8_t *p = heap_malloc(vlen);
            if (!p) return -1;
            memcpy(p, value, vlen);
            out->data     = p;
            out->data_len = vlen;
            break;
        }
        default:
            break;
        }

        used += n;
    }

    return 0;
}

static struct rpc_buf *rpc_buf_alloc(const uint8_t *data, size_t len)
{
    struct rpc_buf *b = heap_malloc(sizeof(*b) + len);
    if (!b)
        return NULL;
    list_node_init(&b->node);
    b->len = len;
    if (len > 0 && data)
        memcpy(b->data, data, len);
    return b;
}

static void rpc_reasm_append(struct rpc_reasm *r, const uint8_t *data, size_t len)
{
    struct rpc_buf *b;
    if (!r || len == 0)
        return;
    b = rpc_buf_alloc(data, len);
    if (!b)
        return;
    list_add_prev(&r->head, &b->node);
    r->total_len += len;
}

static int rpc_reasm_peek_header(struct rpc_reasm *r, struct rpc_header *hdr)
{
    size_t copied_size = 0;
    size_t remain_size;
    size_t take;
    struct rpc_buf *b;
    struct list_node *pos;

    if (!r || !hdr)
        return -1;
    if (r->total_len < sizeof(*hdr))
        return -1;

    pos = r->head.next;
    while (pos != &r->head && copied_size < sizeof(*hdr)) {
        b = container_of(pos, struct rpc_buf, node);
        remain_size = sizeof(*hdr) - copied_size;
        take = (remain_size > b->len) ? b->len : remain_size;
        memcpy((uint8_t *)hdr + copied_size, b->data, take);
        copied_size += take;
        pos = pos->next;
    }

    return (copied_size == sizeof(*hdr)) ? 0 : -1;
}

static void rpc_reasm_consume(struct rpc_reasm *r, uint8_t *out, size_t len)
{
    size_t copied = 0;
    size_t remain;
    size_t take;
    struct rpc_buf *b;
    struct list_node *pos, *n;

    if (!r || len == 0)
        return;

    pos = r->head.next;
    while (pos != &r->head && copied < len) {
        b = container_of(pos, struct rpc_buf, node);
        n = pos->next;

        remain = len - copied;
        take = b->len < remain ? b->len : remain;

        if (out)
            memcpy(out + copied, b->data, take);

        copied += take;

        if (take == b->len) {
            list_remove(&b->node);
            heap_free(b);
        } else {
            memmove(b->data, b->data + take, b->len - take);
            b->len -= take;
            break;
        }

        pos = n;
    }

    r->total_len -= copied;
}

struct rpc_transport_class *rpc_trans_class_create(void *send, void *recv, void *close, void *user)
{
    struct rpc_transport_class *ret = heap_malloc(sizeof(struct rpc_transport_class));
    if (!ret)
        return NULL;

    ret->user  = user;
    ret->send  = send;
    ret->recv  = recv;
    ret->close = close;

    list_node_init(&ret->reasm.head);
    ret->reasm.total_len = 0;

    return ret;
}

void rpc_init(uint8_t pending_cap)
{
    hashmap_init(&g_pending, pending_cap, HASHMAP_KEY_INT);
    g_handler = NULL;
}

void rpc_set_handler(rpc_handler_t h)
{
    rpc_port_lock();
    g_handler = h;
    rpc_port_unlock();
}

static uint32_t rpc_next_seq(void)
{
    uint32_t s;
    do {
        s = g_next_seq++;
        if (g_next_seq == 0)
            g_next_seq = 1;
    } while (hashmap_contains(&g_pending, (void*)(uintptr_t)s));
    return s;
}

static int rpc_send_message(struct rpc_transport_class *t,
                            uint32_t seq,
                            rpc_status_t status,
                            const uint8_t *payload,
                            size_t payload_len)
{
    size_t total;
    struct rpc_message *msg;
    int ret;

    if (!t || !t->send)
        return -1;

    total = sizeof(struct rpc_header) + payload_len;
    msg = heap_malloc(total);
    if (!msg)
        return -1;

    msg->hdr.status  = htons((uint16_t)status);
    msg->hdr.seq     = htonl(seq);
    msg->hdr.msg_len = htonl((uint32_t)payload_len);

    if (payload_len > 0 && payload)
        memcpy(msg->payload, payload, payload_len);

    rpc_debug_dump_tx_response(seq, status, msg, total);
    ret = (int)t->send(t->user, (const uint8_t *)msg, total);
    heap_free(msg);
    return ret;
}

static void rpc_handle_response(const struct rpc_message *msg, size_t len)
{
    if (len < sizeof(struct rpc_header))
        return;

    uint32_t seq     = ntohl(msg->hdr.seq);
    uint32_t msg_len = ntohl(msg->hdr.msg_len);

    if (len < sizeof(struct rpc_header) + msg_len)
        return;

    struct pending_call *pc =
        hashmap_get(&g_pending, (void *)(uintptr_t)seq);

    if (!pc)
        return;

    pc->status = (rpc_status_t)ntohs(msg->hdr.status);

    if (pc->status == RPC_STATUS_OK && msg_len > 0) {
        if (decode_response_tlv(msg->payload, msg_len, pc->out) != 0) {
            pc->status = RPC_STATUS_INTERNAL_ERROR;
        }
    }

    rpc_waiter_wake(pc->waiter);
}

static void rpc_handle_request(struct rpc_transport_class *t,
                               const struct rpc_message *msg, size_t len)
{
    if (len < sizeof(struct rpc_header))
        return;

    uint32_t seq     = ntohl(msg->hdr.seq);
    uint32_t msg_len = ntohl(msg->hdr.msg_len);

    if (len < sizeof(struct rpc_header) + msg_len)
        return;

    rpc_handler_t h = g_handler;
    if (!h) {
        rpc_send_message(t, seq, RPC_STATUS_INTERNAL_ERROR, NULL, 0);
        return;
    }

    struct rpc_request  req;
    struct rpc_response resp;
    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    if (decode_request_tlv(msg->payload, msg_len, &req) != 0) {
        rpc_send_message(t, seq, RPC_STATUS_INTERNAL_ERROR, NULL, 0);
        return;
    }

    rpc_port_set_current_request(&req);
    rpc_port_set_current_response(&resp);

    int rc = h(&req, &resp);

    rpc_port_set_current_request(NULL);
    rpc_port_set_current_response(NULL);

    rpc_status_t status = (rc == 0) ? RPC_STATUS_OK : RPC_STATUS_INTERNAL_ERROR;

    uint8_t *resp_tlv = NULL;
    size_t   resp_len = 0;

    if (status == RPC_STATUS_OK) {
        resp_len = encode_response_tlv(NULL, &resp);
        resp_tlv = heap_malloc(resp_len);
        if (resp_tlv)
            encode_response_tlv(resp_tlv, &resp);
    }

    rpc_send_message(
        t,
        seq,
        status,
        (status == RPC_STATUS_OK && resp_tlv) ? resp_tlv : NULL,
        (status == RPC_STATUS_OK && resp_tlv) ? resp_len : 0
    );

    rpc_free_request(&req);
    rpc_free_response(&resp);
    if (resp_tlv)
        heap_free(resp_tlv);
}

static void rpc_dispatch_message(struct rpc_transport_class *t,
                                 const uint8_t *buf, size_t len)
{
    const struct rpc_message *msg = (const struct rpc_message *)buf;
    uint32_t seq;

    if (len < sizeof(struct rpc_header))
        return;

    seq = ntohl(msg->hdr.seq);

    if (hashmap_contains(&g_pending, (void *)(uintptr_t)seq))
        rpc_handle_response(msg, len);
    else
        rpc_handle_request(t, msg, len);
}

void rpc_on_data(struct rpc_transport_class *t,
                 const uint8_t *buf, size_t len)
{
    size_t n;
    struct rpc_reasm *r;
    struct rpc_header hdr;
    uint32_t msg_len;
    size_t need;
    uint8_t *msg_buf;

    if (!t)
        return;

    rpc_debug_dump_rx(buf, len);

    rpc_port_lock();

    r = &t->reasm;

    if (len > 0) {
        rpc_reasm_append(r, buf, len);
    } else {
        n = t->recv(t->user, poll_buf, sizeof(poll_buf));
        if (n > 0)
            rpc_reasm_append(r, poll_buf, (size_t)n);
        else {
            rpc_port_unlock();
            return;
        }
    }

    for (;;) {
        if (r->total_len < sizeof(struct rpc_header))
            break;

        if (rpc_reasm_peek_header(r, &hdr) < 0)
            break;

        msg_len = ntohl(hdr.msg_len);
        need    = sizeof(struct rpc_header) + (size_t)msg_len;

        if (r->total_len < need)
            break;

        msg_buf = heap_malloc(need);
        if (!msg_buf) {
            rpc_reasm_consume(r, NULL, need);
            continue;
        }

        rpc_reasm_consume(r, msg_buf, need);
        rpc_dispatch_message(t, msg_buf, need);
        heap_free(msg_buf);
    }

    rpc_port_unlock();
}

int rpc_call(struct rpc_transport_class *t,
             const struct rpc_request *in,
             struct rpc_response *out,
             uint32_t timeout_ms)
{
    if (!t || !in || !out)
        return -RPC_STATUS_TRANSPORT_ERROR;

    size_t req_len = encode_request_tlv(NULL, in);
    uint8_t *req_tlv = heap_malloc(req_len);
    if (!req_tlv)
        return -RPC_STATUS_INTERNAL_ERROR;

    encode_request_tlv(req_tlv, in);

    rpc_port_lock();

    uint32_t seq = rpc_next_seq();

    struct pending_call *pc = heap_malloc(sizeof(struct pending_call));
    if (!pc) {
        rpc_port_unlock();
        heap_free(req_tlv);
        return -RPC_STATUS_INTERNAL_ERROR;
    }

    pc->seq    = seq;
    pc->out    = out;
    pc->status = RPC_STATUS_OK;
    pc->waiter = rpc_waiter_create();

    if (!pc->waiter) {
        heap_free(pc);
        rpc_port_unlock();
        heap_free(req_tlv);
        return -RPC_STATUS_INTERNAL_ERROR;
    }

    hashmap_put(&g_pending, (void *)(uintptr_t)seq, pc);

    int r = rpc_send_message(t, seq, RPC_STATUS_OK, req_tlv, req_len);
    heap_free(req_tlv);

    if (r < 0) {
        hashmap_remove(&g_pending, (void *)(uintptr_t)seq);
        rpc_waiter_destroy(pc->waiter);
        heap_free(pc);
        rpc_port_unlock();
        return -RPC_STATUS_TRANSPORT_ERROR;
    }

    rpc_port_unlock();

    int wait_ret = rpc_waiter_wait(pc->waiter, timeout_ms);

    rpc_port_lock();

    hashmap_remove(&g_pending, (void *)(uintptr_t)seq);
    rpc_waiter_destroy(pc->waiter);

    int status = (wait_ret == 0) ? pc->status : RPC_STATUS_TIMEOUT;

    heap_free(pc);

    rpc_port_unlock();

    return status;
}

void rpc_free_request(struct rpc_request *r)
{
    if (!r) return;
    if (r->path) heap_free(r->path);
    if (r->args) heap_free(r->args);
    if (r->data) heap_free(r->data);
    r->path = NULL;
    r->args = NULL;
    r->data = NULL;
    r->data_len = 0;
}

void rpc_free_response(struct rpc_response *r)
{
    if (!r) return;
    if (r->output) heap_free(r->output);
    if (r->data)   heap_free(r->data);
    r->output   = NULL;
    r->data     = NULL;
    r->data_len = 0;
}
