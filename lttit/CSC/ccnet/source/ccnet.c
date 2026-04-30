#include "ccnet.h"
#include "lttit_config.h"
#include "common.h"
#include "hashmap.h"
#include "heap.h"
#include <string.h>
#include <stdio.h>

static void ccnet_debug_hex(const char *tag, const uint8_t *buf, int len)
{
#if CCNET_DEBUG
    printf("%s (%d): ", tag, len);
    for (int i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");
#endif    
}

static void ccnet_debug_in(const uint8_t *buf, int len)
{
    ccnet_debug_hex("CCNET_IN", buf, len);
}

static void ccnet_debug_out(const uint8_t *buf, int len)
{
    ccnet_debug_hex("CCNET_OUT", buf, len);
}

struct ccnet_private {
    struct ccnet_graph g_base;
    struct hashmap link_map;
    uint8_t next_hop[CCNET_MAX_NODES];
    uint16_t src;
    uint8_t node_count;
};

static struct ccnet_private cc;
static struct ccnet_dijkstra dij;

static uint8_t rx_buf[CCNET_MAX_PACKET];
static uint16_t rx_pos = 0;
static uint16_t rx_need = sizeof(struct ccnet_hdr);
static uint8_t rx_state = 0;

void ccnet_graph_init(struct ccnet_graph *g, int n)
{
    g->node_count = n;
    for (int i = 0; i < n; i++)
        for (int j = 0; j < n; j++)
            g->cost[i][j] = (i == j) ? 0 : CCNET_INF;
}

void ccnet_graph_set_edge(int u, int v, int w)
{
    if (u < 0 || v < 0 || u >= cc.node_count || v >= cc.node_count) return;
    cc.g_base.cost[u][v] = w;
}

static void run_dij(struct ccnet_graph *g, int src)
{
    int n = g->node_count;

    for (int i = 0; i < n; i++) {
        dij.dist[i] = CCNET_INF;
        dij.prev[i] = -1;
        dij.visited[i] = 0;
    }
    dij.dist[src] = 0;

    for (int iter = 0; iter < n; iter++) {
        int u = -1, best = CCNET_INF;
        for (int i = 0; i < n; i++)
            if (!dij.visited[i] && dij.dist[i] < best)
                best = dij.dist[i], u = i;

        if (u == -1) break;
        dij.visited[u] = 1;

        for (int v = 0; v < n; v++) {
            int w = g->cost[u][v];
            if (w >= CCNET_INF) continue;
            if (dij.dist[u] + w < dij.dist[v]) {
                dij.dist[v] = dij.dist[u] + w;
                dij.prev[v] = u;
            }
        }
    }
}

static int next_hop_from_dij(int src, int dst)
{
    if (dst == src) return src;
    if (dij.dist[dst] >= CCNET_INF) return -1;

    int cur = dst;
    int p = dij.prev[cur];
    if (p == -1) return -1;

    while (p != src && p != -1) {
        cur = p;
        p = dij.prev[cur];
    }
    return (p == -1) ? -1 : cur;
}

int ccnet_init(uint16_t src, int node_count)
{
    if (node_count <= 0 || node_count > CCNET_MAX_NODES) return -1;
    if (src >= node_count) return -1;

    memset(&cc, 0, sizeof(cc));
    cc.src = src;
    cc.node_count = node_count;

    ccnet_graph_init(&cc.g_base, node_count);
    hashmap_init(&cc.link_map, 16, HASHMAP_KEY_INT);

    for (int i = 0; i < CCNET_MAX_NODES; i++)
        cc.next_hop[i] = 0xFF;

    return 0;
}

void ccnet_build_routing_table(void)
{
    run_dij(&cc.g_base, cc.src);

    for (int dst = 0; dst < cc.node_count; dst++) {
        int nh = next_hop_from_dij(cc.src, dst);
        cc.next_hop[dst] = (nh < 0) ? 0xFF : (uint8_t)nh;
    }
}

int ccnet_register_node_link(uint16_t node_id, ccnet_link_t fun)
{
    hashmap_put(&cc.link_map, (void *)(uintptr_t)node_id, fun);
    return 0;
}

static void send_raw(uint16_t nh, void *data, uint16_t len)
{
#if CCNET_DEBUG
    ccnet_debug_out((const uint8_t *)data, len);
#endif

    ccnet_link_t f = hashmap_get(&cc.link_map, (void *)(uintptr_t)nh);
    if (f) f(NULL, data, len);
}

int ccnet_output(void *ctx, void *data, int len)
{
    struct ccnet_send_parameter *p = ctx;
    uint16_t dst = p->dst;
    uint8_t ttl = p->ttl ? p->ttl : CCNET_TTL_DEFAULT;

    if (dst >= cc.node_count) return -1;

    uint8_t nh = cc.next_hop[dst];
    if (nh == 0xFF) return -1;

    uint16_t all_len = sizeof(struct ccnet_hdr) + len;
    struct ccnet_hdr *h = heap_malloc(all_len);
    if (!h) return -1;

    h->src = htons(cc.src);
    h->dst = htons(dst);
    h->ttl = ttl;
    h->type = p->type;
    h->len = htons(len);
    h->checksum = 0;

    memcpy(h + 1, data, len);
    h->checksum = in_checksum(h, all_len);

    send_raw(nh, h, all_len);
    heap_free(h);
    return 0;
}

static int route(struct ccnet_hdr *h, void *data, int len)
{
    uint16_t dst = ntohs(h->dst);
    if (dst >= cc.node_count) return -1;

    uint8_t nh = cc.next_hop[dst];
    if (nh == 0xFF) return -1;

    send_raw(nh, data, len);
    return 0;
}

static void deliver(void *data)
{
    struct ccnet_hdr *h = data;
    uint16_t me = cc.src;

    ccnet_link_t f = hashmap_get(&cc.link_map, (void *)(uintptr_t)me);
    if (!f) return;

    ccnet_debug_in((uint8_t *)(h + 1), ntohs(h->len));
    f(NULL, (void *)(h + 1), ntohs(h->len));
}

int ccnet_input(void *ctx, void *data, int len)
{
    ccnet_debug_in((uint8_t *)data, len);

    if (len < (int)sizeof(struct ccnet_hdr)) return -1;

    struct ccnet_hdr *h = data;
    if (in_checksum(data, len) != 0) return -1;

    if (h->ttl == 0) return 0;
    h->ttl--;

    uint16_t dst = ntohs(h->dst);
    if (dst != cc.src)
        return route(h, data, len);

    deliver(data);
    return 0;
}
