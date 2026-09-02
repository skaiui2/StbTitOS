#include <string.h>
#include <stdio.h>
#include "vim.h"
#include "fs.h"
#include "comm.h"
#include "heap.h"

struct line {
    char *data;
    int len;
};

struct buffer {
    struct line *lines;
    int line_count;
    int cursor_x;
    int cursor_y;
};

static struct line empty_line = { .data = "", .len = 0 };
static struct buffer empty_buf = {
        .lines = &empty_line,
        .line_count = 1,
        .cursor_x = 0,
        .cursor_y = 0,
};

static char *dup_str(const char *s)
{
    size_t len = strlen(s) + 1;
    char *p = heap_malloc(len);
    if (!p) return NULL;
    memcpy(p, s, len);
    return p;
}

static char *grow_line(char *old, int old_len, int new_len)
{
    char *p = heap_malloc(new_len);
    if (!p) return NULL;
    if (old && old_len > 0)
        memcpy(p, old, old_len);
    if (old)
        heap_free(old);
    return p;
}

static struct line *grow_lines(struct line *old, int old_count, int new_count)
{
    size_t sz = new_count * sizeof(struct line);
    struct line *p = heap_malloc(sz);
    if (!p) return NULL;

    if (old && old_count > 0) {
        memcpy(p, old, old_count * sizeof(struct line));
        heap_free(old);
    }
    return p;
}

void buf_insert_char(struct buffer *b, char c)
{
    struct line *ln = &b->lines[b->cursor_y];

    char *tmp = grow_line(ln->data, ln->len + 1, ln->len + 2);
    if (!tmp) return;

    ln->data = tmp;

    memmove(ln->data + b->cursor_x + 1,
            ln->data + b->cursor_x,
            ln->len - b->cursor_x + 1);

    ln->data[b->cursor_x] = c;
    ln->len++;
    b->cursor_x++;
}

void buf_backspace(struct buffer *b)
{
    struct line *ln = &b->lines[b->cursor_y];

    if (b->cursor_x == 0) {
        if (b->cursor_y == 0)
            return;

        int prev = b->cursor_y - 1;
        struct line *pl = &b->lines[prev];

        char *tmp = grow_line(pl->data, pl->len + 1, pl->len + ln->len + 1);
        if (!tmp) return;

        pl->data = tmp;
        memcpy(pl->data + pl->len, ln->data, ln->len + 1);
        pl->len += ln->len;

        heap_free(ln->data);

        memmove(&b->lines[b->cursor_y],
                &b->lines[b->cursor_y + 1],
                (b->line_count - b->cursor_y - 1) * sizeof(struct line));

        b->line_count--;
        b->cursor_y--;
        b->cursor_x = pl->len - ln->len;
        return;
    }

    memmove(ln->data + b->cursor_x - 1,
            ln->data + b->cursor_x,
            ln->len - b->cursor_x + 1);

    ln->len--;
    b->cursor_x--;
}

void buf_newline(struct buffer *b)
{
    struct line *ln = &b->lines[b->cursor_y];

    char *right = dup_str(ln->data + b->cursor_x);
    if (!right) return;

    int right_len = strlen(right);

    ln->data[b->cursor_x] = 0;
    ln->len = b->cursor_x;

    struct line *tmp = grow_lines(b->lines, b->line_count, b->line_count + 1);
    if (!tmp) {
        heap_free(right);
        return;
    }

    b->lines = tmp;

    memmove(&b->lines[b->cursor_y + 2],
            &b->lines[b->cursor_y + 1],
            (b->line_count - b->cursor_y - 1) * sizeof(struct line));

    b->lines[b->cursor_y + 1].data = right;
    b->lines[b->cursor_y + 1].len  = right_len;

    b->line_count++;
    b->cursor_y++;
    b->cursor_x = 0;
}

void buf_delete_char(struct buffer *b)
{
    struct line *ln = &b->lines[b->cursor_y];
    if (b->cursor_x >= ln->len)
        return;

    memmove(ln->data + b->cursor_x,
            ln->data + b->cursor_x + 1,
            ln->len - b->cursor_x);

    ln->len--;
}

int buf_load(struct buffer *b, const char *path)
{
    struct inode *ino;

    if (fs_open(path, 0, &ino) < 0) {
        b->lines = heap_malloc(sizeof(struct line));
        if (!b->lines) return -1;

        b->lines[0].data = dup_str("");
        b->lines[0].len  = 0;
        b->line_count    = 1;
        b->cursor_x      = 0;
        b->cursor_y      = 0;
        return 0;
    }

    uint32_t fsize = fs_get_size(ino);
    char small[VIM_SMALL_LOAD_BUF];
    char *buf = NULL;
    char *heapbuf = NULL;

    if (fsize + 1 <= sizeof(small)) {
        buf = small;
    } else {
        heapbuf = heap_malloc(fsize + 1);
        if (!heapbuf) {
            fs_close(ino);
            return -1;
        }
        buf = heapbuf;
    }

    int r = fs_read(ino, 0, buf, fsize);
    fs_close(ino);

    if (r < 0) r = 0;
    buf[r] = 0;

    b->line_count = 0;
    b->lines = NULL;

    char *save_ptr;
    char *p = strtok_r(buf, "\n", &save_ptr);

    while (p) {
        struct line *new_lines =
                grow_lines(b->lines, b->line_count, b->line_count + 1);
        if (!new_lines) {
            if (heapbuf) heap_free(heapbuf);
            return -1;
        }

        b->lines = new_lines;
        b->lines[b->line_count].data = dup_str(p);
        b->lines[b->line_count].len  = strlen(p);
        b->line_count++;

        p = strtok_r(NULL, "\n", &save_ptr);
    }

    if (b->line_count == 0) {
        b->lines = heap_malloc(sizeof(struct line));
        b->lines[0].data = dup_str("");
        b->lines[0].len  = 0;
        b->line_count    = 1;
    }

    b->cursor_x = 0;
    b->cursor_y = 0;

    if (heapbuf)
        heap_free(heapbuf);

    return 0;
}

int buf_save(struct buffer *b, const char *path)
{
    struct inode *ino;

    if (fs_open(path, O_CREAT, &ino) < 0)
        return -1;

    uint32_t total = 0;
    for (int i = 0; i < b->line_count; i++)
        total += b->lines[i].len + 1;

    if (total <= VIM_SMALL_SAVE_BUF) {
        char tmp[VIM_SMALL_SAVE_BUF];
        uint32_t off = 0;

        for (int i = 0; i < b->line_count; i++) {
            memcpy(tmp + off, b->lines[i].data, b->lines[i].len);
            off += b->lines[i].len;
            tmp[off++] = '\n';
        }

        fs_truncate(ino, 0);
        int w = fs_write(ino, 0, tmp, total);
        fs_close(ino);

        if (w < 0 || (uint32_t)w != total)
            return -1;

        return 0;
    }

    char *mem = heap_malloc(total);
    if (!mem) {
        fs_close(ino);
        return -1;
    }

    uint32_t off = 0;
    for (int i = 0; i < b->line_count; i++) {
        memcpy(mem + off, b->lines[i].data, b->lines[i].len);
        off += b->lines[i].len;
        mem[off++] = '\n';
    }

    fs_truncate(ino, 0);
    int w = fs_write(ino, 0, mem, total);
    heap_free(mem);
    fs_close(ino);

    if (w < 0 || (uint32_t)w != total)
        return -1;

    return 0;
}

void buf_free(struct buffer *b)
{
    if (!b->lines) return;

    for (int i = 0; i < b->line_count; i++)
        heap_free(b->lines[i].data);

    heap_free(b->lines);
    b->lines = NULL;
    b->line_count = 0;
}

static void screen_draw(struct buffer *b, int insert_mode)
{
    char buf[VIM_SCREEN_BUF];

    comm_write("\x1b[2J", 4);
    comm_write("\x1b[H", 3);

    for (int i = 0; i < b->line_count; i++) {
        comm_write(b->lines[i].data, b->lines[i].len);
        comm_write("\r\n", 2);
    }

    int n = snprintf(buf, sizeof(buf), "\x1b[%d;1H", b->line_count + 2);
    if (n > 0) comm_write(buf, n);

    if (insert_mode)
        comm_write("-- INSERT --", 12);

    n = snprintf(buf, sizeof(buf),
                 "\x1b[%d;%dH",
                 b->cursor_y + 1,
                 b->cursor_x + 1);
    if (n > 0) comm_write(buf, n);
}

void vim_main(const char *path)
{
    struct buffer buf = {0};

    if (buf_load(&buf, path) < 0)
        return;

    int insert_mode = 0;

    for (;;) {
        if (buf.cursor_y >= buf.line_count)
            buf.cursor_y = buf.line_count - 1;
        if (buf.cursor_y < 0)
            buf.cursor_y = 0;

        if (buf.cursor_x > buf.lines[buf.cursor_y].len)
            buf.cursor_x = buf.lines[buf.cursor_y].len;
        if (buf.cursor_x < 0)
            buf.cursor_x = 0;

        screen_draw(&buf, insert_mode);

        char c = comm_getc();

        if ((unsigned char)c == 0x1B) {
            insert_mode = 0;
            continue;
        }

        if (!insert_mode) {
            if (c == 'i') insert_mode = 1;
            else if (c == 'h') { if (buf.cursor_x > 0) buf.cursor_x--; }
            else if (c == 'l') { if (buf.cursor_x < buf.lines[buf.cursor_y].len) buf.cursor_x++; }
            else if (c == 'j') { if (buf.cursor_y < buf.line_count - 1) buf.cursor_y++; }
            else if (c == 'k') { if (buf.cursor_y > 0) buf.cursor_y--; }
            else if (c == 'x') buf_delete_char(&buf);
            else if (c == ':') {
                char cmd[VIM_CMD_BUF_SIZE];
                int pos = 0;
                char tmp[VIM_SCREEN_BUF];
                int n = snprintf(tmp, sizeof(tmp),
                                 "\x1b[%d;1H:",
                                 buf.line_count + 2);
                if (n > 0) comm_write(tmp, n);

                for (;;) {
                    char k = comm_getc();
                    if (k == '\r' || k == '\n') {
                        cmd[pos] = 0;
                        break;
                    }
                    if (pos < VIM_CMD_BUF_SIZE - 1) {
                        cmd[pos++] = k;
                        comm_putc(k);
                    }
                }

                if (strcmp(cmd, "w") == 0) {
                    buf_save(&buf, path);
                    fs_sync();
                }

                else if (strcmp(cmd, "q") == 0) {
                    screen_draw(&empty_buf, 0);
                    comm_write("\r\n", 2);
                    buf_free(&buf);
                    return;
                }
                continue;
            }
            continue;
        }
        if (c == '\r' || c == '\n') {
            buf_newline(&buf);
            continue;
        }
        if (c == 127 || c == '\b') {
            buf_backspace(&buf);
            continue;
        }
        buf_insert_char(&buf, c);
    }
}