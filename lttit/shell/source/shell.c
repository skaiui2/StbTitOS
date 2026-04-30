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
    memset(path, 0, sizeof(path));

    if (argc > 1)
        make_abs_path(path, argv[1]);
    else
        strcpy(path, cwd);

    struct dirent *ents =
            heap_malloc(sizeof(struct dirent) * SHELL_LS_MAX_ENTRIES);
    if (!ents)
        return -1;

    if (path[0] == '\0')
        strcpy(path, "/");

    int n = 0;
    if (fs_readdir(path, ents, SHELL_LS_MAX_ENTRIES, &n) < 0) {
        heap_free(ents);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        comm_write(ents[i].name, (int)strlen(ents[i].name));
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
#endif

#if SHELL_ENABLE_VIM
        {"vim",    cmd_vim},
#endif

        {"mem",    cmd_mem},
        {"ps",     cmd_ps},
        {"remote", cmd_remote},
        {"memleak", cmd_memleak},
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
