#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "shell.h"
#if SHELL_ENABLE_VIM
#include "vim.h"
#endif

#if SHELL_ENABLE_FS
#include "fs.h"
#endif

#include "comm.h"
#include "schedule.h"
#include "heap.h"
#include "scp.h"
#include "fs.h"
#include "world.h"
#include "rpc.h"

#if SHELL_ENABLE_COMPILER
#include "lexer.h"
#include "parser.h"
#include "ir_lowering.h"
#include "ccbpf.h"
#include "sem.h"
#endif

#if PICO_2W
#include "pico/bootrom.h"

int cmd_boot(int argc, char **argv)
{
    printf("rebooting to BOOTSEL...\n");

    reset_usb_boot(0, 0);

    return 0;
}
#endif

static char linebuf[SHELL_MAX_LINE];
static char path[SHELL_MAX_PATH];
static char cwd[SHELL_MAX_PATH] = "";
static char *argv_buf[SHELL_MAX_ARGS];
static char shell_abs[SHELL_MAX_PATH];

static void normalize_path(char *p)
{
    char *src = p;
    char *dst = p;

    if (*src != '/')
        return;

    while (*src) {
        if (src[0] == '/' && src[1] == '/') {
            src++;
            continue;
        }

        if (src[0] == '/' && src[1] == '.' &&
            (src[2] == '/' || src[2] == '\0')) {
            src += 2;
            continue;
        }

        if (src[0] == '/' && src[1] == '.' && src[2] == '.' &&
            (src[3] == '/' || src[3] == '\0')) {

            if (dst != p) {
                dst--;
                while (dst > p && *dst != '/')
                    dst--;
            }
            src += 3;
            continue;
        }

        *dst++ = *src++;
    }

    if (dst == p) {
        *dst++ = '/';
    }

    if (dst > p + 1 && *(dst - 1) == '/')
        dst--;

    *dst = '\0';
}

static void make_abs_path(char *out, const char *in)
{
    if (in[0] == '/') {
        snprintf(out, SHELL_MAX_PATH, "%s", in);
    } else if (cwd[0] == '\0' || (cwd[0] == '/' && cwd[1] == '\0')) {
        snprintf(out, SHELL_MAX_PATH, "/%s", in);
    } else {
        snprintf(out, SHELL_MAX_PATH, "%s/%s", cwd, in);
    }

    normalize_path(out);
}

#if SHELL_ENABLE_FS
static int fs_is_dir(const char *p)
{
    struct dirent tmp[1];
    int nread = 0;
    return fs_readdir(p, tmp, 1, &nread) == 0;
}
#endif

int shell_readline(char *buf, int max)
{
    int pos = 0;

    for (;;) {
        char c = comm_getc();

        if (c == '\r' || c == '\n') {
            comm_putc('\r');
            comm_putc('\n');
            buf[pos] = 0;
            return pos;
        }

        if (c == '\b' || c == 127) {
            if (pos > 0) {
                pos--;
                comm_write(SHELL_BACKSPACE_SEQ, SHELL_BACKSPACE_SEQ_LEN);
            }
            continue;
        }

        if (pos < max - 1) {
            buf[pos++] = c;
            comm_putc(c);
        }
    }
}

int shell_parse(char *line, char **argv, int max)
{
    int argc = 0;

    while (*line && argc < max) {
        while (*line == ' ')
            line++;
        if (!*line)
            break;

        argv[argc++] = line;

        while (*line && *line != ' ')
            line++;
        if (*line)
            *line++ = 0;
    }

    return argc;
}

#if SHELL_ENABLE_FS
int cmd_ls(int argc, char **argv)
{
    int n = 0;
    struct dirent *ents;

    if (argc > 1)
        make_abs_path(path, argv[1]);
    else
        strcpy(path, cwd);

    if (path[0] == '\0')
        strcpy(path, "/");

    ents = heap_malloc(sizeof(struct dirent) * SHELL_LS_MAX_ENTRIES);
    if (!ents) return -1;

    if (fs_readdir(path, ents, SHELL_LS_MAX_ENTRIES, &n) < 0) {
        heap_free(ents);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        comm_write(ents[i].name, strlen(ents[i].name));
        comm_write("  ", 2);
    }

    comm_write("\r\n", 2);
    heap_free(ents);
    return 0;
}

int cmd_cat(int argc, char **argv)
{
    if (argc < 2)
        return -1;

    memset(path, 0, sizeof(path));
    make_abs_path(path, argv[1]);

    struct inode *ino;
    if (fs_open(path, 0, &ino) < 0)
        return -1;

    uint32_t off = 0;
    int r;
    char *cat_buf = heap_malloc(SHELL_CAT_BUF_SIZE);
    if (!cat_buf) {
        fs_close(ino);
        return -1;
    }

    while ((r = fs_read(ino, off, cat_buf, SHELL_CAT_BUF_SIZE)) > 0) {
        for (int i = 0; i < r; i++) {
            if (cat_buf[i] == '\n')
                comm_write("\r\n", 2);
            else
                comm_putc(cat_buf[i]);
        }
        off += (uint32_t)r;
    }

    heap_free(cat_buf);
    fs_close(ino);
    return 0;
}

int cmd_touch(int argc, char **argv)
{
    if (argc < 2)
        return -1;

    memset(path, 0, sizeof(path));
    make_abs_path(path, argv[1]);

    struct inode *ino;
    if (fs_open(path, O_CREAT, &ino) < 0)
        return -1;

    fs_close(ino);
    return 0;
}

int cmd_mkdir(int argc, char **argv)
{
    if (argc < 2)
        return -1;

    memset(path, 0, sizeof(path));
    make_abs_path(path, argv[1]);

    struct inode *ino;
    if (fs_mkdir(path, &ino) < 0)
        return -1;

    return 0;
}

int cmd_cd(int argc, char **argv)
{
    if (argc < 2)
        return -1;

    memset(path, 0, sizeof(path));
    make_abs_path(path, argv[1]);

    if (!fs_is_dir(path))
        return -1;

    strcpy(cwd, path);
    return 0;
}

int cmd_sync(int argc, char **argv)
{
    if (argc != 1)
        return -1;

    fs_sync();
    return 0;
}

int cmd_rm(int argc, char **argv)
{
    if (argc < 2)
        return -1;

    memset(path, 0, sizeof(path));
    make_abs_path(path, argv[1]);

    if (fs_unlink(path) < 0) {
        printf("rm failed\n");
        return -1;
    }

    return 0;
}
#endif

#if SHELL_ENABLE_VIM
int cmd_vim(int argc, char **argv)
{
    if (argc < 2)
        return -1;

    memset(shell_abs, 0, sizeof(shell_abs));
    make_abs_path(shell_abs, argv[1]);

    vim_main(shell_abs);
    return 0;
}
#endif

int cmd_mem(int argc, char **argv)
{
    struct heap_stats st = heap_get_stats();
    char buf[128];

    int n = snprintf(buf, sizeof(buf),
                     "heap_remain: %u\r\n"
                     "heap_free_iter: %u\r\n"
                     "heap_max_block: %u\r\n"
                     "heap_free_blocks: %u\r\n",
                     st.remain_size,
                     st.free_size_iter,
                     st.max_free_block,
                     st.free_blocks);

    if (n > 0)
        comm_write(buf, n);

    return 0;
}

int cmd_ps(int argc, char **argv)
{
    char buf[160];
    struct task_info info;

    comm_write("PID   STATE      STACK_USED   PERIOD   DEADLINE\r\n", 52);

    for (uint32_t pid = 1; pid < TASK_COUNT; pid++) {
        if (rtos_get_task_info(pid, &info) != 0)
            continue;

        const char *state_str =
                (info.state == RUNNING)   ? "RUNNING"  :
                (info.state == Ready)     ? "READY"    :
                (info.state == OS_Delay)  ? "DELAYED"  :
                (info.state == Suspend)   ? "SUSPEND"  :
                (info.state == Dead)      ? "DELETED"  :
                "UNKNOWN";

        int n = snprintf(buf, sizeof(buf),
                         "%-5u %-10s %-12u %-8u %-8u\r\n",
                         info.pid,
                         state_str,
                         info.stack_watermark,
                         info.period,
                         info.deadline);

        if (n > 0)
            comm_write(buf, n);
    }

    return 0;
}

int cmd_remote(int argc, char **argv)
{
    if (argc < 3)
        return -1;

    char buf[SHELL_REMOTE_MAX_CMD];
    size_t pos = 0;

    buf[0] = 0;

    for (int i = 2; i < argc; i++) {
        size_t len = strlen(argv[i]);
        if (pos + len + 1 >= sizeof(buf))
            break;

        memcpy(buf + pos, argv[i], len);
        pos += len;

        if (i != argc - 1 && pos + 1 < sizeof(buf))
            buf[pos++] = ' ';
    }

    buf[pos] = 0;

    scp_send(1, buf, (int)pos);
    return 0;
}

int cmd_memleak(int argc, char **argv)
{
    heap_debug_dump_leaks();
    return 0;
}

extern struct rpc_transport_class *g_rpc_transport;

static int cmd_open(int argc, char **argv)
{
    if (argc < 2) return -1;

    struct rpc_request req = {0};
    struct rpc_response resp = {0};

    req.op   = RPC_OP_OPEN;
    req.path = argv[1];

    rpc_call(g_rpc_transport, &req, &resp, 10000);
    rpc_free_response(&resp);
    return 0;
}

static int cmd_close(int argc, char **argv)
{
    if (argc < 2) return -1;

    struct rpc_request req = {0};
    struct rpc_response resp = {0};

    req.op   = RPC_OP_CLOSE;
    req.path = argv[1];

    rpc_call(g_rpc_transport, &req, &resp, 10000);
    rpc_free_response(&resp);
    return 0;
}

static int cmd_read(int argc, char **argv)
{
    if (argc < 2) return -1;

    struct rpc_request req = {0};
    struct rpc_response resp = {0};

    req.op   = RPC_OP_READ;
    req.path = argv[1];

    rpc_call(g_rpc_transport, &req, &resp, 10000);

    if (resp.output)
        comm_write(resp.output, strlen(resp.output));

    rpc_free_response(&resp);
    return 0;
}

static int cmd_write(int argc, char **argv)
{
    if (argc < 3) return -1;

    struct rpc_request req = {0};
    struct rpc_response resp = {0};

    req.op   = RPC_OP_WRITE;
    req.path = argv[1];
    req.args = argv[2];

    rpc_call(g_rpc_transport, &req, &resp, 10000);
    rpc_free_response(&resp);
    return 0;
}

struct tnode {
    char *name;
    struct tnode *child[64];
    int child_count;
};

static struct tnode root_node;

static struct tnode *add_child(struct tnode *p, char *name)
{
    for (int i = 0; i < p->child_count; i++)
        if (strcmp(p->child[i]->name, name) == 0)
            return p->child[i];

    struct tnode *n = heap_malloc(sizeof(struct tnode));
    if (!n) return NULL;
    memset(n, 0, sizeof(*n));
    n->name = name;
    p->child[p->child_count++] = n;
    return n;
}

static void out(const char *s)
{
    comm_write(s, strlen(s));
}

static void print_tree(struct tnode *p, int depth, int last)
{
    if (depth > 0) {
        for (int i = 1; i < depth; i++)
            out("    ");
        out(last ? "└── " : "├── ");
        out(p->name);
        out("\r\n");
    }

    for (int i = 0; i < p->child_count; i++)
        print_tree(p->child[i], depth + 1, i == p->child_count - 1);
}

static int cmp(const void *a, const void *b)
{
    struct tnode * const *x = a;
    struct tnode * const *y = b;
    return strcmp((*x)->name, (*y)->name);
}

static void sort_tree(struct tnode *p)
{
    if (p->child_count > 1)
        qsort(p->child, p->child_count, sizeof(p->child[0]), cmp);
    for (int i = 0; i < p->child_count; i++)
        sort_tree(p->child[i]);
}

static void world_print_tree(void)
{
    char *buf = heap_malloc(2048);
    if (!buf) return;

    struct dump_ctx c = { buf, 2048, 0 };
    if (world_dump(&c) <= 0) {
        heap_free(buf);
        return;
    }

    memset(&root_node, 0, sizeof(root_node));

    char *p = buf;
    char *end = buf + c.pos;

    while (p < end) {
        char *nl = memchr(p, '\n', end - p);
        if (!nl) break;
        *nl = 0;

        char *s = p;
        if (*s == '/') s++;

        struct tnode *cur = &root_node;

        while (*s) {
            char *slash = strchr(s, '/');
            if (!slash) {
                add_child(cur, s);
                break;
            }
            *slash = 0;
            add_child(cur, s);
            cur = add_child(cur, s);
            s = slash + 1;
        }

        p = nl + 1;
    }

    sort_tree(&root_node);
    print_tree(&root_node, 0, 1);

    heap_free(buf);
}

int cmd_tree(int argc, char **argv)
{
    world_print_tree();
    return 0;
}

#if SHELL_ENABLE_COMPILER
static char *load_text_file_heap_fs(const char *path)
{
    struct inode *ino;
    if (fs_open(path, O_RDONLY, &ino) != 0)
        return NULL;

    uint32_t size = fs_get_size(ino);
    char *buf = heap_malloc(size + 1);
    if (!buf) {
        fs_close(ino);
        return NULL;
    }

    int r = fs_read(ino, 0, buf, size);
    fs_close(ino);
    if (r != (int)size) {
        heap_free(buf);
        return NULL;
    }

    buf[size] = 0;
    return buf;
}

extern void native_init_frontend(void);
static int cmd_compile(int argc, char **argv)
{
    if (argc < 2)
        return -1;

    char src_path[SHELL_MAX_PATH];
    char out_path[SHELL_MAX_PATH];

    memset(src_path, 0, sizeof(src_path));
    make_abs_path(src_path, argv[1]);

    char *src = load_text_file_heap_fs(src_path);
    if (!src) {
        comm_write("load source failed\r\n", 21);
        return 0;
    }
    
    compiler_init(16, 20*1024, 1*1024, 15*1024);
    struct lexer lex;
    lexer_init(&lex);
    lexer_set_input_buffer(src, strlen(src));

    struct Parser *p = parser_new(&lex);
    native_init_frontend();
    parser_program(p);

    frontend_destroy(&lex);

    struct bpf_builder b;
    bpf_builder_init(&b, 20*1024);

    struct ir_mes im;
    ir_mes_get(&im);
    ir_lower_program(im.ir_head, im.label_count, &b);

    struct bpf_insn *prog = bpf_builder_data(&b);
    int prog_len = bpf_builder_count(&b);

    size_t image_len = 0;
    uint8_t *image = ccbpf_pack_memory(prog, (size_t)prog_len, &image_len);

    bpf_builder_free(&b);
    heap_free(src);

    if (!image) {
        comm_write("pack image failed\r\n", 21);
        return 0;
    }

    const char *in = argv[1];
    const char *dot = strrchr(in, '.');
    if (!dot) dot = in + strlen(in);

    int n = (int)(dot - in);
    if (n >= SHELL_MAX_PATH - 8) n = SHELL_MAX_PATH - 8;

    memcpy(out_path, in, n);
    out_path[n] = 0;
    strcat(out_path, ".ccbpf");

    char abs_out[SHELL_MAX_PATH];
    make_abs_path(abs_out, out_path);

    struct inode *ino;
    if (fs_open(abs_out, O_CREAT | O_RDWR, &ino) != 0) {
        comm_write("fs_open failed\r\n", 17);
        heap_free(image);
        return 0;
    }

    int w = fs_write(ino, 0, image, (uint32_t)image_len);
    fs_close(ino);
    fs_sync();

    heap_free(image);

    char msg[128];
    int m = snprintf(msg, sizeof(msg),
                     "compiled to %s, %d bytes\r\n",
                     abs_out, w);
    if (m > 0) comm_write(msg, m);

    return 0;
}

static struct ccbpf_program *g_prog;
static struct ccbpf_ctx      g_ctx;
static uint8_t              *g_img;
static size_t                g_img_len;

static semaphore_handle sem_migrate;

static int cmd_runbpf(int argc, char **argv)
{
    if (argc < 2) return -1;

    char path[SHELL_MAX_PATH];
    make_abs_path(path, argv[1]);

    struct inode *ino;
    if (fs_open(path, O_RDONLY, &ino) != 0) {
        printf("open failed\n");
        return 0;
    }

    uint32_t size = fs_get_size(ino);
    g_img = heap_malloc(size);
    g_img_len = size;

    fs_read(ino, 0, g_img, size);
    fs_close(ino);

    g_prog = ccbpf_load_from_memory(g_img, g_img_len);
    memset(&g_ctx, 0, sizeof(g_ctx));

    if (!sem_migrate)
        sem_migrate = semaphore_create(0);

    unsigned char p[1] = {0};
    for (;;) {
        enum ccbpf_status st =
            ccbpf_vm_step(&g_ctx, g_prog, p, 1, 1, 64);

        if (st == CCBPF_FINISHED) {
            printf("Pico: finished %u\n", g_ctx.ret);
            break;
        }

        if (st == CCBPF_MIGRATE) {
            printf("Pico: migrate pc=%u\n", g_ctx.pc);
            semaphore_release(sem_migrate);
            break;
        }

        if (st == CCBPF_ERROR) {
            printf("Pico: error\n");
            break;
        }
    }

    return 0;
}

void task_migrate_sender(void *arg)
{
    (void)arg;

    for (;;) {
        if (semaphore_take(sem_migrate, 0xFFFF)) {

            uint8_t *ctx_buf = NULL;
            size_t ctx_len = 0;
            ccbpf_ctx_pack(&g_ctx, &ctx_buf, &ctx_len);

            size_t total = 8 + g_img_len + ctx_len;
            uint8_t *buf = heap_malloc(total);

            uint8_t *p = buf;
            *(uint32_t *)p = g_img_len; p += 4;
            *(uint32_t *)p = ctx_len;   p += 4;
            memcpy(p, g_img, g_img_len); p += g_img_len;
            memcpy(p, ctx_buf, ctx_len);

            struct rpc_request req = {0};
            struct rpc_response resp = {0};

            req.op       = RPC_OP_WRITE;
            req.path     = "/root/nodeB/vm/migrate";
            req.data     = buf;
            req.data_len = total;
            printf("req.data_len:%u\r\n", total);

            rpc_call(g_rpc_transport, &req, &resp, 10000);

            rpc_free_response(&resp);
            heap_free(buf);
            heap_free(ctx_buf);

            printf("Pico: migrate packet sent\n");
        }
    }
}

#endif

struct cmd_entry {
    const char *name;
    int (*func)(int argc, char **argv);
};

static struct cmd_entry cmd_table[] = {

#if SHELL_ENABLE_FS
        {"ls",     cmd_ls},
        {"cat",    cmd_cat},
        {"touch",  cmd_touch},
        {"mkdir",  cmd_mkdir},
        {"cd",     cmd_cd},
        {"sync",   cmd_sync},
        {"rm", cmd_rm},
#endif

#if SHELL_ENABLE_VIM
        {"vim",    cmd_vim},
#endif

        {"mem",    cmd_mem},
        {"ps",     cmd_ps},
        {"remote", cmd_remote},
        {"memleak", cmd_memleak},
        {"open",  cmd_open},
        {"close", cmd_close},
        {"read",  cmd_read},
        {"write", cmd_write},
        {"tree", cmd_tree},

#if SHELL_ENABLE_COMPILER        
        {"compile", cmd_compile},
        {"runbpf", cmd_runbpf},
#endif

#if PICO_2W
        {"boot", cmd_boot},
#endif        
        {NULL,     NULL}
};

void shell_exec(int argc, char **argv)
{
    for (int i = 0; cmd_table[i].name; i++) {
        if (strcmp(argv[0], cmd_table[i].name) == 0) {
            cmd_table[i].func(argc, argv);
            return;
        }
    }

    char buf[64];
    int n = snprintf(buf, sizeof(buf),
                     "unknown command: %s\r\n", argv[0]);
    if (n > 0)
        comm_write(buf, n);
}

void shell_main(void)
{
    sem_migrate = semaphore_create(0);
    task_create(task_migrate_sender, 1024, NULL, 0, 60, NULL);

    comm_write(SHELL_PROMPT, (int)strlen(SHELL_PROMPT));

    int len = shell_readline(linebuf, SHELL_MAX_LINE);
    if (len <= 0)
        return;

    int argc = shell_parse(linebuf, argv_buf, SHELL_MAX_ARGS);
    if (argc == 0)
        return;

    shell_exec(argc, argv_buf);
}

void shell_on_message(const char *msg, int len)
{
    if (!msg || len <= 0)
        return;

    if (len >= SHELL_MAX_LINE)
        len = SHELL_MAX_LINE - 1;

    memcpy(linebuf, msg, (size_t)len);
    linebuf[len] = '\0';

    int argc = shell_parse(linebuf, argv_buf, SHELL_MAX_ARGS);
    if (argc == 0)
        return;

    shell_exec(argc, argv_buf);
}
