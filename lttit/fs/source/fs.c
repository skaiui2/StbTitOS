#include <string.h>
#include "fs.h"
#include "heap.h"

/*
 * 0 for superblock,
 * 1 for inode bitmap,
 * 2 for block bitmap,
 * 3 for inode,
 * 4 for first dir
 */

struct fs_blkdev *g_bdev = NULL;
static struct inode root_inode;
struct inode *g_root = &root_inode;

#define BITS_PER_WORD       32
#define BLOCK_BITMAP_COUNT  ((FS_BLOCK_COUNT + BITS_PER_WORD - 1) / BITS_PER_WORD)
#define INODE_COUNT         (FS_BLOCK_SIZE / (sizeof(struct dinode)))
#define INODE_BITMAP_COUNT  ((INODE_COUNT - 1) / 32 + 1)

static uint32_t BlockBitmap[BLOCK_BITMAP_COUNT];
static uint32_t InodeBitmap[INODE_BITMAP_COUNT];
static struct dinode DInodeArray[INODE_COUNT];

#define DIR_COUNT_MAX (FS_BLOCK_SIZE / sizeof(struct dirent))
static struct dirent ents[DIR_COUNT_MAX];

//just for temp, because i don't want to use heap_malloc
static char path_copy[PATH_LEN_MAX];
static char parent_path[PATH_LEN_MAX];
static char filename[NAME_MAX];


static void split_path(const char *path, char *parent, char *name)
{
    int len;
    int i;

    parent[0] = '\0';
    name[0]   = '\0';

    if (!path || path[0] == '\0') {
        return;
    }

    len = (int)strlen(path);
    while (len > 1 && path[len - 1] == '/')
        len--;

    i = len - 1;
    while (i >= 0 && path[i] != '/')
        i--;

    if (len <= i + 1) {
        name[0] = '\0';
    } else {
        strncpy(name, path + i + 1, NAME_MAX);
        name[NAME_MAX - 1] = '\0';
    }

    if (i <= 0) {
        strcpy(parent, "/");
    } else {
        strncpy(parent, path, i);
        parent[i] = '\0';
    }
}

void bitmap_init(uint32_t *bitmap, uint32_t count)
{
    uint32_t i;

    for (i = 0; i < count; i++)
        bitmap[i] = ~0U;
}

void bitmap_set_free(uint32_t *bitmap, uint32_t blk)
{
    uint32_t idx;
    uint32_t bit;

    idx = blk / BITS_PER_WORD;
    bit = blk % BITS_PER_WORD;
    bitmap[idx] |= (1U << bit);
}

void bitmap_set_used(uint32_t *bitmap, uint32_t blk)
{
    uint32_t idx;
    uint32_t bit;

    idx = blk / BITS_PER_WORD;
    bit = blk % BITS_PER_WORD;
    bitmap[idx] &= ~(1U << bit);
}

int bitmap_find_free(const uint32_t *bitmap, uint32_t bit_word_count)
{
    uint32_t i;
    uint32_t b;

    for (i = 0; i < bit_word_count; i++) {
        uint32_t word = bitmap[i];

        if (word != 0) {
            for (b = 0; b < BITS_PER_WORD; b++) {
                if (word & (1U << b))
                    return (int)(i * BITS_PER_WORD + b);
            }
        }
    }
    return -1;
}

int block_alloc(void)
{
    int blk;

    blk = bitmap_find_free(BlockBitmap, BLOCK_BITMAP_COUNT);
    if (blk < 0)
        return -1;

    bitmap_set_used(BlockBitmap, (uint32_t)blk);
    return blk;
}

int block_free(uint32_t blk)
{
    bitmap_set_free(BlockBitmap, blk);
    return 0;
}

int block_write(uint32_t blk, uint32_t off, const void *buf, uint32_t size)
{
    return g_bdev->write(g_bdev->ctx, blk, off, buf, size);
}

int inode_alloc(struct inode **out)
{
    int i;
    int j;
    struct inode *ino;

    i = bitmap_find_free(InodeBitmap, INODE_BITMAP_COUNT);
    if (i < 0)
        return -1;

    ino = heap_malloc(sizeof(struct inode));
    if (!ino)
        return -1;

    bitmap_set_used(InodeBitmap, (uint32_t)i);

    ino->ino = (uint32_t)i;
    ino->din = DInodeArray[i];
    ino->din.mode = 0;
    ino->din.size = 0;

    for (j = 0; j < NDIRECT; j++)
        ino->din.direct[j] = 0xFFFFFFFFU;

    ino->refcnt = 1;
    *out = ino;
    return 0;
}

int inode_free(struct inode *inode)
{
    if (!inode)
        return -1;

    bitmap_set_free(InodeBitmap, inode->ino);
    memset(&DInodeArray[inode->ino], 0xFF, sizeof(struct dinode));

    heap_free(inode);
    return 0;
}

int dir_is_exist(uint32_t blk, char *token, struct dirent *out)
{
    uint32_t i;

    if (g_bdev->read(g_bdev->ctx, blk, 0, ents, sizeof(ents)) != 0)
        return -1;

    for (i = 0; i < DIR_COUNT_MAX; i++) {
        if (ents[i].name[0] == 0xFF)
            continue;

        if (strcmp(ents[i].name, token) == 0) {
            if (out)
                memcpy(out, &ents[i], sizeof(struct dirent));
            return 1;
        }
    }

    return 0;
}

int dir_add_entry(struct inode *dir, const char *name, uint32_t ino, uint8_t type)
{
    uint32_t blk;
    int count;
    int i;

    blk = dir->din.direct[0];
    count = DIR_COUNT_MAX;

    if (g_bdev->read(g_bdev->ctx, blk, 0, ents, sizeof(ents)) != 0)
        return -1;

    for (i = 0; i < count; i++) {
        if (strcmp(ents[i].name, name) == 0)
            return -1;

        if (ents[i].name[0] == 0xFF) {
            strncpy(ents[i].name, name, NAME_MAX);
            ents[i].name[NAME_MAX - 1] = '\0';

            ents[i].ino = ino;
            ents[i].type = type;

            if (g_bdev->erase(g_bdev->ctx, blk) != 0)
                return -1;
            if (g_bdev->write(g_bdev->ctx, blk, 0, ents, sizeof(ents)) != 0)
                return -1;

            return 0;
        }
    }

    return -1;
}

int dir_lookup(struct inode *dir, const char *name, struct dirent *out)
{
    uint32_t blk;
    int count;
    int i;

    blk = dir->din.direct[0];
    count = FS_BLOCK_SIZE / sizeof(struct dirent);

    if (g_bdev->read(g_bdev->ctx, blk, 0, ents, FS_BLOCK_SIZE) != 0)
        return 0;

    for (i = 0; i < count; i++) {
        if (ents[i].name[0] == 0xFF)
            continue;

        if (strcmp(ents[i].name, name) == 0) {
            if (out)
                memcpy(out, &ents[i], sizeof(struct dirent));
            return 1;
        }
    }

    return 0;
}

static int fs_lookup_path(const char *path, struct inode **out)
{
    char tmp[PATH_LEN_MAX];
    char *save_ptr;
    char *token;
    struct inode *cur;
    struct dirent de;
    uint32_t blk;

    if (!path || path[0] == '\0')
        return -1;

    if (strcmp(path, "/") == 0) {
        g_root->refcnt++;
        *out = g_root;
        return 0;
    }

    if (path[0] == '/')
        path++;

    memset(tmp, 0, sizeof(tmp));
    strncpy(tmp, path, PATH_LEN_MAX);
    tmp[PATH_LEN_MAX - 1] = '\0';

    cur = heap_malloc(sizeof(struct inode));
    if (!cur)
        return -1;
    cur->ino    = 0;
    cur->din    = DInodeArray[0];
    cur->refcnt = 1;

    token = strtok_r(tmp, "/", &save_ptr);

    while (token) {
        if (strcmp(token, ".") == 0) {
            token = strtok_r(NULL, "/", &save_ptr);
            continue;
        }

        if (strcmp(token, "..") == 0) {
            if (cur->ino != 0) {
                blk = cur->din.direct[0];
                if (g_bdev->read(g_bdev->ctx, blk, 0, ents, sizeof(ents)) != 0) {
                    heap_free(cur);
                    return -1;
                }

                heap_free(cur);
                cur = heap_malloc(sizeof(struct inode));
                if (!cur)
                    return -1;
                cur->ino    = ents[1].ino;
                cur->din    = DInodeArray[cur->ino];
                cur->refcnt = 1;
            }
            token = strtok_r(NULL, "/", &save_ptr);
            continue;
        }

        if (!dir_lookup(cur, token, &de)) {
            heap_free(cur);
            return -1;
        }

        heap_free(cur);
        cur = heap_malloc(sizeof(struct inode));
        if (!cur)
            return -1;
        cur->ino    = de.ino;
        cur->din    = DInodeArray[de.ino];
        cur->refcnt = 1;

        token = strtok_r(NULL, "/", &save_ptr);
    }

    *out = cur;
    return 0;
}

int fs_mount(struct superblock *sb, struct fs_blkdev *bdev)
{
    g_bdev = bdev;

    if (g_bdev->read(g_bdev->ctx, 0, 0, sb, sizeof(struct superblock)) != 0)
        return -1;
    if (sb->magic != 0x12345678)
        return -2;
    if (g_bdev->read(g_bdev->ctx, 1, 0, InodeBitmap, sizeof(InodeBitmap)) != 0)
        return -1;
    if (g_bdev->read(g_bdev->ctx, 2, 0, BlockBitmap, sizeof(BlockBitmap)) != 0)
        return -1;
    if (g_bdev->read(g_bdev->ctx, 3, 0, DInodeArray, sizeof(DInodeArray)) != 0)
        return -1;

    g_root->ino = 0;
    g_root->refcnt = 1;
    g_root->din = DInodeArray[0];

    return 0;
}

int fs_unmount(struct superblock *sb)
{
    if (!sb || !g_bdev)
        return -1;

    if (g_root)
        g_root = NULL;

    if (g_bdev->sync)
        g_bdev->sync(g_bdev->ctx);

    g_bdev = NULL;
    return 0;
}

int fs_format(struct superblock *sb)
{
    uint32_t i;
    int blk;
    struct dirent *de;

    bitmap_init(BlockBitmap, BLOCK_BITMAP_COUNT);
    bitmap_init(InodeBitmap, INODE_BITMAP_COUNT);
    memset(DInodeArray, 0xFF, sizeof(DInodeArray));

    sb->magic        = 0x12345678;
    sb->block_size   = g_bdev->block_size;
    sb->total_blocks = g_bdev->block_count;
    sb->inode_start  = 1;
    sb->inode_count  = sb->block_size / sizeof(struct dinode);
    sb->data_start   = sb->inode_start + 1;

    if (g_bdev->erase) {
        for (i = 0; i < g_bdev->block_count; i++)
            g_bdev->erase(g_bdev->ctx, i);
    }

    for (i = 0; i < 4; i++)
        bitmap_set_used(BlockBitmap, i);

    bitmap_set_used(InodeBitmap, 0);

    g_root        = &root_inode;
    g_root->ino   = 0;
    g_root->refcnt = 1;
    g_root->din.mode = S_IFDIR;
    g_root->din.size = sb->block_size;

    blk = block_alloc();
    if (blk < 0)
        return -1;

    g_root->din.direct[0] = (uint32_t)blk;
    DInodeArray[g_root->ino] = g_root->din;

    memset(ents, 0xFF, sizeof(ents));

    de = &ents[0];
    de->type = FILE_TYPE_DIR;
    de->ino  = 0;
    strncpy(de->name, ".", NAME_MAX);
    de->name[NAME_MAX - 1] = '\0';

    de = &ents[1];
    de->type = FILE_TYPE_DIR;
    de->ino  = 0;
    strncpy(de->name, "..", NAME_MAX);
    de->name[NAME_MAX - 1] = '\0';

    if (g_bdev->write(g_bdev->ctx, 0, 0, sb, sizeof(struct superblock)) != 0)
        return -1;
    if (g_bdev->write(g_bdev->ctx, 1, 0, InodeBitmap, sizeof(InodeBitmap)) != 0)
        return -1;
    if (g_bdev->write(g_bdev->ctx, 2, 0, BlockBitmap, sizeof(BlockBitmap)) != 0)
        return -1;
    if (g_bdev->write(g_bdev->ctx, 3, 0, DInodeArray, sizeof(DInodeArray)) != 0)
        return -1;
    if (g_bdev->write(g_bdev->ctx, (uint32_t)blk, 0, ents, sizeof(ents)) != 0)
        return -1;

    return 0;
}

int fs_sync(void)
{
    if (g_bdev->write(g_bdev->ctx, 1, 0, InodeBitmap, sizeof(InodeBitmap)) != 0)
        return -1;
    if (g_bdev->write(g_bdev->ctx, 2, 0, BlockBitmap, sizeof(BlockBitmap)) != 0)
        return -1;
    if (g_bdev->write(g_bdev->ctx, 3, 0, DInodeArray, sizeof(DInodeArray)) != 0)
        return -1;

    if (g_bdev->sync)
        g_bdev->sync(g_bdev->ctx);

    return 0;
}

int fs_mkdir(const char *path, struct inode **ino)
{
    char *saveptr;
    char *token;
    struct inode *cur;
    struct inode *parent;
    struct dirent dir;
    struct inode *new_inode = NULL;
    uint32_t data_blk;
    uint32_t blk;
    int exist;

    if (!path || path[0] == '\0')
        return -1;

    if (strcmp(path, "/") == 0) {
        g_root->refcnt++;
        *ino = g_root;
        return 0;
    }

    if (path[0] == '/')
        path++;

    memset(path_copy, 0, PATH_LEN_MAX);
    strncpy(path_copy, path, PATH_LEN_MAX);
    path_copy[PATH_LEN_MAX - 1] = '\0';

    cur = heap_malloc(sizeof(struct inode));
    if (!cur)
        return -1;
    cur->ino    = 0;
    cur->din    = DInodeArray[0];
    cur->refcnt = 1;

    parent = NULL;
    token = strtok_r(path_copy, "/", &saveptr);

    while (token) {
        parent = cur;
        data_blk = parent->din.direct[0];

        exist = dir_is_exist(data_blk, token, &dir);
        if (exist < 0) {
            heap_free(parent);
            return -1;
        }

        if (exist == 1) {
            cur = heap_malloc(sizeof(struct inode));
            if (!cur) {
                if (parent != g_root)
                    heap_free(parent);
                return -1;
            }
            cur->ino    = dir.ino;
            cur->din    = DInodeArray[dir.ino];
            cur->refcnt = 1;

            if (parent != g_root)
                heap_free(parent);

            parent = cur;
            token = strtok_r(NULL, "/", &saveptr);
            continue;
        }

        break;
    }

    if (!token) {
        *ino = parent;
        return 0;
    }

    while (token) {
        if (inode_alloc(&new_inode) < 0) {
            if (parent && parent != g_root)
                heap_free(parent);
            return -1;
        }

        blk = (uint32_t)block_alloc();
        if (blk == (uint32_t)-1) {
            inode_free(new_inode);
            if (parent && parent != g_root)
                heap_free(parent);
            return -1;
        }

        memset(ents, 0xFF, sizeof(ents));

        ents[0].type = FILE_TYPE_DIR;
        ents[0].ino  = new_inode->ino;
        strncpy(ents[0].name, ".", NAME_MAX);
        ents[0].name[NAME_MAX - 1] = '\0';

        ents[1].type = FILE_TYPE_DIR;
        ents[1].ino  = parent ? parent->ino : 0;
        strncpy(ents[1].name, "..", NAME_MAX);
        ents[1].name[NAME_MAX - 1] = '\0';

        if (g_bdev->erase && g_bdev->erase(g_bdev->ctx, blk) != 0) {
            inode_free(new_inode);
            if (parent && parent != g_root)
                heap_free(parent);
            return -1;
        }

        if (g_bdev->write(g_bdev->ctx, blk, 0, ents, sizeof(ents)) != 0) {
            inode_free(new_inode);
            if (parent && parent != g_root)
                heap_free(parent);
            return -1;
        }

        new_inode->din.direct[0] = blk;
        new_inode->din.mode      = S_IFDIR;
        new_inode->din.size      = FS_BLOCK_SIZE;
        DInodeArray[new_inode->ino] = new_inode->din;

        if (dir_add_entry(parent, token, new_inode->ino, FILE_TYPE_DIR) < 0) {
            inode_free(new_inode);
            if (parent && parent != g_root)
                heap_free(parent);
            return -1;
        }

        if (parent && parent != g_root)
            heap_free(parent);

        parent = new_inode;
        token = strtok_r(NULL, "/", &saveptr);
    }

    *ino = new_inode;
    return 0;
}

int fs_rmdir(struct superblock *sb, const char *path)
{
    char parent_path[PATH_LEN_MAX];
    char name[NAME_MAX];
    struct inode *parent;
    struct dirent de;
    uint32_t blk;
    uint32_t i;
    int exist;

    (void)sb;

    if (!path || strcmp(path, "/") == 0)
        return -1;

    memset(parent_path, 0, PATH_LEN_MAX);
    memset(name, 0, NAME_MAX);
    split_path(path, parent_path, name);

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    if (fs_lookup_path(parent_path, &parent) < 0)
        return -1;

    exist = dir_is_exist(parent->din.direct[0], name, &de);
    if (exist <= 0) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }

    if (de.type != FILE_TYPE_DIR) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }

    blk = DInodeArray[de.ino].direct[0];
    if (g_bdev->read(g_bdev->ctx, blk, 0, ents, sizeof(ents)) != 0) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }

    for (i = 2; i < DIR_COUNT_MAX; i++) {
        if (ents[i].name[0] != 0xFF) {
            if (parent != g_root) heap_free(parent);
            return -1;
        }
    }

    if (g_bdev->read(g_bdev->ctx, parent->din.direct[0], 0, ents, sizeof(ents)) != 0) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }

    for (i = 0; i < DIR_COUNT_MAX; i++) {
        if (strcmp(ents[i].name, name) == 0) {
            ents[i].name[0] = 0xFF;
            break;
        }
    }

    if (g_bdev->erase && g_bdev->erase(g_bdev->ctx, parent->din.direct[0]) != 0) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }
    if (g_bdev->write(g_bdev->ctx, parent->din.direct[0], 0, ents, sizeof(ents)) != 0) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }

    block_free(blk);
    bitmap_set_free(InodeBitmap, de.ino);
    memset(&DInodeArray[de.ino], 0xFF, sizeof(struct dinode));

    if (parent != g_root)
        heap_free(parent);

    return 0;
}

int fs_readdir(const char *path, struct dirent *buf, int max, int *nread)
{
    struct inode *dir_ino;
    uint32_t blk;
    uint32_t i;
    int count;

    *nread = 0;

    if (fs_lookup_path(path, &dir_ino) < 0)
        return -1;

    blk = dir_ino->din.direct[0];

    if (g_bdev->read(g_bdev->ctx, blk, 0, ents, sizeof(ents)) != 0) {
        if (dir_ino != g_root)
            heap_free(dir_ino);
        return -1;
    }

    count = 0;
    for (i = 0; (i < DIR_COUNT_MAX) && (count < max); i++) {
        if ((uint8_t)ents[i].name[0] == 0xFF)
            continue;

        buf[count++] = ents[i];
    }

    *nread = count;

    if (dir_ino != g_root)
        heap_free(dir_ino);

    return 0;
}

int fs_unlink(const char *path)
{
    char parent_path[PATH_LEN_MAX];
    char name[NAME_MAX];
    struct inode *parent;
    struct inode file;
    struct dirent de;
    uint32_t blk;
    uint32_t i;
    int exist;

    if (!path || strcmp(path, "/") == 0)
        return -1;

    memset(parent_path, 0, PATH_LEN_MAX);
    memset(name, 0, NAME_MAX);
    split_path(path, parent_path, name);

    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return -1;

    if (name[0] == '\0')
        return -1;

    if (fs_lookup_path(parent_path, &parent) < 0)
        return -1;

    exist = dir_is_exist(parent->din.direct[0], name, &de);
    if (exist <= 0) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }

    if (de.type != FILE_TYPE_REG) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }

    file.ino = de.ino;
    file.din = DInodeArray[de.ino];

    for (i = 0; i < NDIRECT; i++) {
        if (file.din.direct[i] != 0xFFFFFFFFU) {
            block_free(file.din.direct[i]);
            file.din.direct[i] = 0xFFFFFFFFU;
        }
    }

    bitmap_set_free(InodeBitmap, file.ino);
    memset(&DInodeArray[file.ino], 0xFF, sizeof(struct dinode));

    blk = parent->din.direct[0];
    if (g_bdev->read(g_bdev->ctx, blk, 0, ents, sizeof(ents)) != 0) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }

    for (i = 0; i < DIR_COUNT_MAX; i++) {
        if (strcmp(ents[i].name, name) == 0) {
            ents[i].name[0] = 0xFF;
            break;
        }
    }

    if (g_bdev->erase && g_bdev->erase(g_bdev->ctx, blk) != 0) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }
    if (g_bdev->write(g_bdev->ctx, blk, 0, ents, sizeof(ents)) != 0) {
        if (parent != g_root) heap_free(parent);
        return -1;
    }

    if (parent != g_root)
        heap_free(parent);

    return 0;
}

int fs_open(const char *path, int flags, struct inode **out)
{
    struct inode *parent;
    uint32_t data_blk;
    struct dirent dir;
    int exist;
    struct inode *node;
    struct inode *newfile;
    int i;

    memset(parent_path, 0, PATH_LEN_MAX);
    memset(filename, 0, NAME_MAX);
    split_path(path, parent_path, filename);

    if (filename[0] == '\0')
        return -1;

    if (fs_lookup_path(parent_path, &parent) < 0)
        return -1;

    data_blk = parent->din.direct[0];

    exist = dir_is_exist(data_blk, filename, &dir);
    if (exist < 0) {
        if (parent != g_root)
            heap_free(parent);
        return -1;
    }

    if (exist == 1) {
        if (dir.type == FILE_TYPE_DIR) {
            if (parent != g_root)
                heap_free(parent);
            return -1; 
        }

        if ((flags & O_EXCL) && (flags & O_CREAT)) {
            if (parent != g_root)
                heap_free(parent);
            return -1;
        }

        node = heap_malloc(sizeof(struct inode));
        if (!node) {
            if (parent != g_root)
                heap_free(parent);
            return -1;
        }

        node->din = DInodeArray[dir.ino];
        node->ino = dir.ino;
        node->refcnt = 1;
        *out = node;

        if (parent != g_root)
            heap_free(parent);
        return 0;
    }

    if (!(flags & O_CREAT)) {
        if (parent != g_root)
            heap_free(parent);
        return -1;
    }

    if (inode_alloc(&newfile) < 0) {
        if (parent != g_root)
            heap_free(parent);
        return -1;
    }

    newfile->din.mode = S_IFREG;
    newfile->din.size = 0;
    // I think don't need to alloc block, do by fs_write.
    for (i = 0; i < NDIRECT; i++)
        newfile->din.direct[i] = 0xFFFFFFFFU;

    DInodeArray[newfile->ino] = newfile->din;

    if (dir_add_entry(parent, filename, newfile->ino, FILE_TYPE_REG) < 0) {
        inode_free(newfile);
        if (parent != g_root)
            heap_free(parent);
        return -1;
    }

    *out = newfile;

    if (parent != g_root)
        heap_free(parent);

    return 0;
}

int fs_read(struct inode *inode, uint32_t off, void *buf, uint32_t len)
{
    uint32_t file_size;
    uint8_t *dst;
    uint32_t total;

    file_size = inode->din.size;

    if (off >= file_size)
        return 0;

    if (off + len > file_size)
        len = file_size - off;

    dst = (uint8_t *)buf;
    total = 0;

    while (total < len) {
        uint32_t pos;
        uint32_t blk_index;
        uint32_t blk_off;
        uint32_t blkno;
        uint32_t chunk;

        pos = off + total;
        blk_index = pos / FS_BLOCK_SIZE;
        blk_off = pos % FS_BLOCK_SIZE;

        if (blk_index >= NDIRECT)
            break;

        blkno = inode->din.direct[blk_index];
        if (blkno == 0xFFFFFFFFU)
            break;

        chunk = FS_BLOCK_SIZE - blk_off;
        if (chunk > len - total)
            chunk = len - total;

        if (g_bdev->read(g_bdev->ctx, blkno, blk_off,
                         dst + total, chunk) != 0)
            return -1;

        total += chunk;
    }

    return (int)total;
}

int fs_write(struct inode *inode, uint32_t off, const void *buf, uint32_t len)
{
    const uint8_t *src;
    uint32_t total;
    uint32_t old_size;
    uint8_t *block_cache;

    src = (const uint8_t *)buf;
    total = 0;
    old_size = inode->din.size;

    block_cache = heap_malloc(FS_BLOCK_SIZE);
    if (!block_cache)
        return -1;

    while (total < len) {
        uint32_t pos;
        uint32_t blk_index;
        uint32_t blk_off;
        uint32_t *pblk;
        int newblk;
        uint32_t blkno;
        uint32_t chunk;

        pos = off + total;
        blk_index = pos / FS_BLOCK_SIZE;
        blk_off   = pos % FS_BLOCK_SIZE;

        if (blk_index >= NDIRECT)
            break;

        pblk = &inode->din.direct[blk_index];

        if (*pblk == 0xFFFFFFFFU) {
            newblk = block_alloc();
            if (newblk < 0) {
                heap_free(block_cache);
                return -1;
            }
            *pblk = (uint32_t)newblk;
            memset(block_cache, 0, FS_BLOCK_SIZE);
        } else {
            if (g_bdev->read(g_bdev->ctx, *pblk, 0,
                             block_cache, FS_BLOCK_SIZE) != 0) {
                heap_free(block_cache);
                return -1;
            }
        }

        blkno = *pblk;

        chunk = FS_BLOCK_SIZE - blk_off;
        if (chunk > len - total)
            chunk = len - total;

        memcpy(block_cache + blk_off, src + total, chunk);

        if (g_bdev->erase) {
            if (g_bdev->erase(g_bdev->ctx, blkno) != 0) {
                heap_free(block_cache);
                return -1;
            }
        }

        if (g_bdev->write(g_bdev->ctx, blkno, 0,
                          block_cache, FS_BLOCK_SIZE) != 0) {
            heap_free(block_cache);
            return -1;
        }

        total += chunk;
    }

    if (off + total > old_size)
        inode->din.size = off + total;

    DInodeArray[inode->ino] = inode->din;

    heap_free(block_cache);
    return (int)total;
}

uint32_t fs_get_size(struct inode *inode)
{
    if (!inode)
        return 0;
    return inode->din.size;
}

int fs_truncate(struct inode *inode, uint32_t newsize)
{
    uint32_t i;

    if (!inode)
        return -1;

    if (newsize != 0)
        return -1;

    for (i = 0; i < NDIRECT; i++) {
        if (inode->din.direct[i] != 0xFFFFFFFFU) {
            block_free(inode->din.direct[i]);
            inode->din.direct[i] = 0xFFFFFFFFU;
        }
    }

    inode->din.size = 0;
    DInodeArray[inode->ino] = inode->din;

    return 0;
}

int fs_close(struct inode *inode)
{
    if (!inode)
        return -1;

    if (inode != g_root && --inode->refcnt == 0)
        heap_free(inode);

    return 0;
}