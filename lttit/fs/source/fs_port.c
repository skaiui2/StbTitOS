#include "fs_port.h"
#include "fs_config.h"
#include "fs.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

#define FS_FLASH_OFFSET (2 * 1024 * 1024)
#define FS_FLASH_BASE (XIP_BASE + FS_FLASH_OFFSET)

static uint8_t sector_buf[FLASH_SECTOR_SIZE];

static int fs_bd_read(void *ctx, uint32_t blk, uint32_t off, void *buf, uint32_t len)
{
    uint32_t fs_off = blk * FS_BLOCK_SIZE + off;
    uint32_t addr = FS_FLASH_BASE + fs_off;
    memcpy(buf, (const void *)addr, len);
    return 0;
}

static int fs_bd_write(void *ctx, uint32_t blk, uint32_t off, const void *buf, uint32_t len)
{
    uint32_t fs_start = blk * FS_BLOCK_SIZE + off;
    uint32_t fs_end = fs_start + len;
    uint32_t sec_first = fs_start / FLASH_SECTOR_SIZE;
    uint32_t sec_last = (fs_end - 1) / FLASH_SECTOR_SIZE;

    const uint8_t *src = buf;

    for (uint32_t sec = sec_first; sec <= sec_last; sec++) {
        uint32_t sec_base = sec * FLASH_SECTOR_SIZE;
        uint32_t sec_flash = FS_FLASH_OFFSET + sec_base;
        uint32_t sec_xip = FS_FLASH_BASE + sec_base;

        memcpy(sector_buf, (const void *)sec_xip, FLASH_SECTOR_SIZE);

        uint32_t w_start = fs_start > sec_base ? fs_start : sec_base;
        uint32_t w_end = fs_end < sec_base + FLASH_SECTOR_SIZE ? fs_end : sec_base + FLASH_SECTOR_SIZE;

        uint32_t local_off = w_start - sec_base;
        uint32_t local_len = w_end - w_start;

        memcpy(sector_buf + local_off, src, local_len);

        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(sec_flash, FLASH_SECTOR_SIZE);
        for (uint32_t p = 0; p < FLASH_SECTOR_SIZE; p += FLASH_PAGE_SIZE)
            flash_range_program(sec_flash + p, sector_buf + p, FLASH_PAGE_SIZE);
        restore_interrupts(ints);

        src += local_len;
    }

    return 0;
}

static int fs_bd_erase(void *ctx, uint32_t blk)
{
    uint32_t fs_off = blk * FS_BLOCK_SIZE;
    uint32_t sec = fs_off / FLASH_SECTOR_SIZE;
    uint32_t sec_base = sec * FLASH_SECTOR_SIZE;
    uint32_t sec_flash = FS_FLASH_OFFSET + sec_base;
    uint32_t sec_xip = FS_FLASH_BASE + sec_base;

    uint32_t blk_off = fs_off - sec_base;

    memcpy(sector_buf, (const void *)sec_xip, FLASH_SECTOR_SIZE);
    memset(sector_buf + blk_off, 0xFF, FS_BLOCK_SIZE);

    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(sec_flash, FLASH_SECTOR_SIZE);
    for (uint32_t p = 0; p < FLASH_SECTOR_SIZE; p += FLASH_PAGE_SIZE)
        flash_range_program(sec_flash + p, sector_buf + p, FLASH_PAGE_SIZE);
    restore_interrupts(ints);

    return 0;
}

static int fs_bd_sync(void *ctx)
{
    return 0;
}

struct fs_blkdev fs_dev;
extern struct fs_blkdev *g_bdev;

void fs_port_init(void)
{
    g_bdev = &fs_dev;
    memset(&fs_dev, 0, sizeof(fs_dev));
    fs_dev.read = fs_bd_read;
    fs_dev.write = fs_bd_write;
    fs_dev.erase = fs_bd_erase;
    fs_dev.sync = fs_bd_sync;
    fs_dev.read_size = FS_READ_SIZE;
    fs_dev.prog_size = FS_PROG_SIZE;
    fs_dev.block_size = FS_BLOCK_SIZE;
    fs_dev.block_count = FS_BLOCK_COUNT;
}

int fs_port_mount(struct superblock *sb)
{
    int r = fs_mount(sb, &fs_dev);
    if (r) {
        r = fs_format(sb);
        if (r) return r;
        r = fs_mount(sb, &fs_dev);
    }
    return r;
}

void fs_port_deinit(struct superblock *sb)
{
    fs_unmount(sb);
}
