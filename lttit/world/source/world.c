#include "world.h"
#include "heap.h"
#include "hashmap.h"
#include <string.h>
#include <stdio.h>

#define WORLD_HASH_BUCKETS 16
#define WORLD_NAME_MAX 64
#define WORLD_LINE_MAX 256
#define WORLD_READ_BUF 512
#define WORLD_PREFIX_MAX (WORLD_NAME_MAX - 1)
#define WORLD_RPC_TIMEOUT 10000

struct world_tree g_world;

static struct hashmap name_to_transport;
static char local_node[WORLD_NAME_MAX];

static const char *norm(const char *p)
{
    while (*p == '/') p++;
    return p;
}

static struct vnode *vnode_new(struct file_ops *ops, void *u)
{
    struct vnode *n;
    n = heap_malloc(sizeof(struct vnode));
    if (!n) return 0;
    n->ops = ops;
    n->userdata = u;
    return n;
}

void world_init(const char *name)
{
    prefix_map_init(&g_world.tree);
    hashmap_init(&name_to_transport, WORLD_HASH_BUCKETS, HASHMAP_KEY_STRING);
    if (!name) name = "";
    strncpy(local_node, name, WORLD_PREFIX_MAX);
    local_node[WORLD_PREFIX_MAX] = 0;
}

int world_register(const char *path, struct file_ops *ops, void *u)
{
    const char *p;
    struct vnode *n;
    p = norm(path);
    n = prefix_map_get(&g_world.tree, p);
    if (n) {
        n->ops = ops;
        n->userdata = u;
        return 0;
    }
    n = vnode_new(ops, u);
    if (!n) return -1;
    if (prefix_map_set(&g_world.tree, p, n) < 0) {
        heap_free(n);
        return -1;
    }
    return 0;
}

struct vnode *world_lookup(const char *path)
{
    if (!path) return 0;
    return prefix_map_get(&g_world.tree, norm(path));
}

static int dump_cb(const char *key, void *val, void *arg)
{
    struct dump_ctx *c;
    int left, n;
    (void)val;
    c = arg;
    left = c->len - c->pos;
    n = snprintf(c->buf + c->pos, left, "%s\n", key);
    if (n <= 0 || n >= left) return -1;
    c->pos += n;
    return 0;
}

int world_dump(struct dump_ctx *ctx)
{
    prefix_map_iter(&g_world.tree, dump_cb, ctx);
    return ctx->pos;
}

void world_add_route(const char *name, void *t)
{
    hashmap_put(&name_to_transport, (void *)name, t);
}

static void *world_route(const char *name)
{
    if (strcmp(name, local_node) == 0) return 0;
    return hashmap_get(&name_to_transport, (void *)name);
}

static int root_open(void *u, const char *p, int f)
{
    return 0;
}

static int root_write(void *u, int fd, const void *buf, int len)
{
    const char *p, *end, *nl;
    int l;
    char line[WORLD_LINE_MAX];
    (void)u;
    (void)fd;
    p = buf;
    end = p + len;
    while (p < end) {
        nl = memchr(p, '\n', end - p);
        l = nl ? (int)(nl - p) : (int)(end - p);
        if (l > 0 && l < WORLD_LINE_MAX) {
            memcpy(line, p, l);
            line[l] = 0;
            world_register(line, 0, 0);
        }
        p += l + 1;
    }
    return len;
}

static int root_read(void *u, int fd, void *buf, int len)
{
    struct dump_ctx c;
    (void)u;
    (void)fd;
    c.buf = buf;
    c.len = len;
    c.pos = 0;
    return world_dump(&c);
}

static int root_close(void *u, int fd)
{
    return 0;
}

static struct file_ops root_ops = {
    root_open,
    root_read,
    root_write,
    0,
    root_close
};

struct file_ops *world_root_ops(void)
{
    return &root_ops;
}

static char *dupstr(const char *s)
{
    size_t n;
    char *p;
    n = strlen(s);
    p = heap_malloc(n + 1);
    if (!p) return 0;
    memcpy(p, s, n + 1);
    return p;
}

static void parse(const char *path, char *prefix, const char **local_path, int *is_local)
{
    const char *slash;
    int plen;
    while (*path == '/') path++;
    slash = strchr(path, '/');
    if (!slash) {
        prefix[0] = 0;
        *local_path = path;
        *is_local = 1;
        return;
    }
    plen = (int)(slash - path);
    if (plen >= WORLD_PREFIX_MAX) plen = WORLD_PREFIX_MAX;
    memcpy(prefix, path, plen);
    prefix[plen] = 0;
    *local_path = slash + 1;
    *is_local = (strcmp(prefix, local_node) == 0);
}

int world_rpc_handle(const struct rpc_request *in, struct rpc_response *out)
{
    char prefix[WORLD_NAME_MAX];
    const char *local_path;
    int is_local;
    struct vnode *n;
    void *t;
    struct rpc_response r;
    int st;
    parse(in->path ? in->path : "", prefix, &local_path, &is_local);
    if (!is_local && prefix[0]) {
        t = world_route(prefix);
        if (!t) {
            out->output = dupstr("NO ROUTE");
            out->exitcode = 1;
            return 0;
        }
        memset(&r, 0, sizeof(r));
        st = rpc_call(t, in, &r, WORLD_RPC_TIMEOUT);
        if (st != RPC_STATUS_OK) {
            out->output = dupstr("FORWARD FAIL");
            out->exitcode = 2;
            return 0;
        }
        *out = r;
        return 0;
    }
    n = world_lookup(local_path);
    if (!n || !n->ops) {
        out->output = dupstr("no such path");
        out->exitcode = 1;
        return 0;
    }
    switch (in->op) {
    case RPC_OP_OPEN:
        n->ops->open(n->userdata, local_path, 0);
        out->output = dupstr("open ok");
        out->exitcode = 0;
        return 0;
    case RPC_OP_READ: {
        char *buf;
        int rlen;
        buf = heap_malloc(WORLD_READ_BUF);
        rlen = n->ops->read(n->userdata, 0, buf, WORLD_READ_BUF - 1);
        if (rlen < 0) {
            heap_free(buf);
            out->output = dupstr("read failed");
            out->exitcode = 2;
            return 0;
        }
        buf[rlen] = 0;
        out->output = buf;
        out->exitcode = 0;
        return 0;
    }
    case RPC_OP_WRITE:
        n->ops->write(n->userdata, 0, in->args ? in->args : "", strlen(in->args ? in->args : ""));
        out->output = dupstr("write ok");
        out->exitcode = 0;
        return 0;
    case RPC_OP_CTL:
        n->ops->ctl(n->userdata, 0, 0, (void *)(in->args ? in->args : ""));
        out->output = dupstr("ctl ok");
        out->exitcode = 0;
        return 0;
    case RPC_OP_CLOSE:
        n->ops->close(n->userdata, 0);
        out->output = dupstr("close ok");
        out->exitcode = 0;
        return 0;
    }
    out->output = dupstr("op not supported");
    out->exitcode = 1;
    return 0;
}
