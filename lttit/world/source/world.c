#include "world.h"
#include "heap.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define WORLD_NAME_MAX 64
#define WORLD_LINE_MAX 256
#define WORLD_READ_BUF 512

struct world_tree g_world;

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

struct dump_node_ctx {
    char *buf;
    int len;
    int pos;
    const char *prefix;
    int prefix_len;
};

static int dump_node_cb(const char *key, void *val, void *arg)
{
    struct dump_node_ctx *c;
    int left, n;
    (void)val;
    c = arg;

    if (strncmp(key, c->prefix, c->prefix_len) != 0)
        return 0;

    left = c->len - c->pos;
    n = snprintf(c->buf + c->pos, left, "%s\n", key);
    if (n <= 0 || n >= left) return -1;
    c->pos += n;
    return 0;
}

int world_dump_node(const char *node_name, char *buf, int len)
{
    struct dump_node_ctx c;
    char prefix[WORLD_NAME_MAX + 16];
    int n;

    n = snprintf(prefix, sizeof(prefix), "root/%s", node_name);
    if (n <= 0) return 0;

    c.buf = buf;
    c.len = len;
    c.pos = 0;
    c.prefix = prefix;
    c.prefix_len = (int)strlen(prefix);

    prefix_map_iter(&g_world.tree, dump_node_cb, &c);
    return c.pos;
}

int world_dump(struct dump_ctx *ctx)
{
    prefix_map_iter(&g_world.tree, dump_cb, ctx);
    return ctx->pos;
}

static int root_open(void *u, const char *p, int f)
{
    (void)u;
    (void)p;
    (void)f;
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
    (void)u;
    (void)fd;
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

int world_rpc_handle(const struct rpc_request *in, struct rpc_response *out)
{
    const char *path = in->path ? in->path : "";
    struct vnode *n = world_lookup(path);
    const char *args;
    char *buf;
    int r, fd, len;

    if (!n || !n->ops) {
        out->output = dupstr("no such path");
        out->exitcode = 1;
        return 0;
    }

    switch (in->op) {

    case RPC_OP_OPEN:
        r = in->args ? atoi(in->args) : 0;
        r = n->ops->open(n->userdata, path, r);
        buf = heap_malloc(32);
        snprintf(buf, 32, "%d", r);
        out->output = buf;
        out->exitcode = 0;
        return 0;

    case RPC_OP_READ: {
        const char *args = in->args ? in->args : "";
        int fd = 0;

        if (strncmp(args, "fd=", 3) == 0)
            fd = atoi(args + 3);

        r = n->ops->read(n->userdata, fd, (void *)args, strlen(args));
        if (r < 0) {
            out->output = dupstr("read failed");
            out->exitcode = 1;
            return 0;
        }

        buf = heap_malloc(32);
        snprintf(buf, 32, "%d", r);
        out->output = buf;
        out->exitcode = 0;
        return 0;
    }

    case RPC_OP_WRITE:
        args = in->args ? in->args : "";
        fd = 0;
        if (strncmp(args, "fd=", 3) == 0)
            fd = atoi(args + 3);
        len = (int)strlen(args);
        r = n->ops->write(n->userdata, fd, args, len);
        buf = heap_malloc(32);
        snprintf(buf, 32, "%d", r);
        out->output = buf;
        out->exitcode = 0;
        return 0;

    case RPC_OP_CLOSE:
        fd = in->args ? atoi(in->args) : 0;
        r = n->ops->close(n->userdata, fd);
        buf = heap_malloc(32);
        snprintf(buf, 32, "%d", r);
        out->output = buf;
        out->exitcode = 0;
        return 0;
    }

    out->output = dupstr("op not supported");
    out->exitcode = 1;
    return 0;
}

void world_init(void)
{
    prefix_map_init(&g_world.tree);
    world_register("root", world_root_ops(), NULL);
    rpc_set_handler(world_rpc_handle);
}

int world_sync_node(const char *node, struct rpc_transport_class *t)
{
    char *buf;
    int n;
    struct rpc_request req;
    struct rpc_response resp;

    buf = heap_malloc(512);
    if (!buf) return -1;

    n = world_dump_node(node, buf, 512);
    if (n <= 0) {
        heap_free(buf);
        return -1;
    }

    memset(&req, 0, sizeof(req));
    memset(&resp, 0, sizeof(resp));

    req.op = RPC_OP_WRITE;
    req.path = "/root";
    req.args = buf;

    rpc_call(t, &req, &resp, 10000);

    heap_free(buf);

    n = resp.exitcode;
    rpc_free_response(&resp);
    return n ? -1 : 0;
}
