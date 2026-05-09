#ifndef WORLD_H
#define WORLD_H

#include "prefix.h"
#include "rpc.h"

struct file_ops {
    int (*open)(void *userdata, const char *path, int flags);
    int (*read)(void *userdata, int fd, void *buf, int len);
    int (*write)(void *userdata, int fd, const void *buf, int len);
    int (*ctl)(void *userdata, int fd, int cmd, void *arg);
    int (*close)(void *userdata, int fd);
};

struct vnode {
    struct file_ops *ops;
    void *userdata;
};

struct world_tree {
    struct prefix_map tree;
};

struct dump_ctx {
    char *buf;
    int len;
    int pos;
};

extern struct world_tree g_world;

void world_init();
int world_register(const char *path, struct file_ops *ops, void *userdata);
struct vnode *world_lookup(const char *path);
int world_dump(struct dump_ctx *ctx);
struct file_ops *world_root_ops(void);
int world_rpc_handle(const struct rpc_request *in, struct rpc_response *out);
int world_dump_node(const char *node_name, char *buf, int len);

#endif
