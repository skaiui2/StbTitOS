#include "fs_port.h"
#include "fs.h"
#include "stm32f1xx_hal.h"
#include <string.h>
#include <stdio.h>

static int fs_bd_read(void *ctx, uint32_t block,
                      uint32_t off, void *buffer, uint32_t size)
{
    (void)ctx;
    uint32_t addr = FS_FLASH_BASE + block * FS_BLOCK_SIZE + off;
    memcpy(buffer, (const void*)addr, size);
    return FS_ERR_OK;
}

static int fs_bd_write(void *ctx, uint32_t block,
                       uint32_t off, const void *buffer, uint32_t size)
{
    (void)ctx;
    uint32_t base_addr = FS_FLASH_BASE + block * FS_BLOCK_SIZE;

    if (off + size > FS_BLOCK_SIZE)
        return FS_ERR_IO;

    uint8_t page_buf[FS_BLOCK_SIZE];
    memcpy(page_buf, (const void*)base_addr, FS_BLOCK_SIZE);
    memcpy(page_buf + off, buffer, size);

    FLASH_EraseInitTypeDef ei;
    memset(&ei, 0, sizeof(ei));
    ei.TypeErase   = FLASH_TYPEERASE_PAGES;
    ei.PageAddress = base_addr;
    ei.NbPages     = 1;

    uint32_t err = 0;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st_erase = HAL_FLASHEx_Erase(&ei, &err);
    if (st_erase != HAL_OK) {
        printf("ERASE_FAIL blk=%lu err=0x%08lX\n",
               (unsigned long)block,
               (unsigned long)err);
        HAL_FLASH_Lock();
        return FS_ERR_IO;
    }

    const uint16_t *p = (const uint16_t*)page_buf;
    uint32_t halfwords = FS_BLOCK_SIZE / 2;

    for (uint32_t i = 0; i < halfwords; i++) {
        uint32_t addr = base_addr + i * 2;
        uint16_t val  = p[i];

        HAL_StatusTypeDef st =
                HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, val);

        if (st != HAL_OK) {
            printf("WRITE_FAIL addr=0x%08lX val=0x%04X st=%d\n",
                   (unsigned long)addr,
                   (unsigned)val,
                   (int)st);
            HAL_FLASH_Lock();
            return FS_ERR_IO;
        }

        if (*(volatile uint16_t*)addr != val) {
            printf("VERIFY_FAIL addr=0x%08lX\n",
                   (unsigned long)addr);
            HAL_FLASH_Lock();
            return FS_ERR_IO;
        }
    }

    HAL_FLASH_Lock();
    return FS_ERR_OK;
}

static int fs_bd_erase(void *ctx, uint32_t block)
{
    (void)ctx;
    uint32_t page_addr = FS_FLASH_BASE + block * FS_BLOCK_SIZE;

    FLASH_EraseInitTypeDef ei;
    memset(&ei, 0, sizeof(ei));
    ei.TypeErase   = FLASH_TYPEERASE_PAGES;
    ei.PageAddress = page_addr;
    ei.NbPages     = 1;

    uint32_t err = 0;
    HAL_FLASH_Unlock();
    HAL_StatusTypeDef st = HAL_FLASHEx_Erase(&ei, &err);
    HAL_FLASH_Lock();

    if (st != HAL_OK) {
        printf("ERASE_FAIL blk=%lu err=0x%08lX\n",
               (unsigned long)block,
               (unsigned long)err);
        return FS_ERR_IO;
    }

    return FS_ERR_OK;
}

static int fs_bd_sync(void *ctx)
{
    (void)ctx;
    return FS_ERR_OK;
}

struct fs_blkdev fs_dev;
extern struct fs_blkdev *g_bdev;

void fs_port_init(void)
{
    g_bdev = &fs_dev;
    memset(&fs_dev, 0, sizeof(fs_dev));

    fs_dev.read  = fs_bd_read;
    fs_dev.write = fs_bd_write;
    fs_dev.erase = fs_bd_erase;
    fs_dev.sync  = fs_bd_sync;

    fs_dev.read_size   = FS_READ_SIZE;
    fs_dev.prog_size   = FS_PROG_SIZE;
    fs_dev.block_size  = FS_BLOCK_SIZE;
    fs_dev.block_count = FS_BLOCK_COUNT;
}

int fs_port_mount(struct superblock *sb)
{
    int err = fs_mount(sb, &fs_dev);
    if (err) {
        err = fs_format(sb);
        if (err) return err;
        err = fs_mount(sb, &fs_dev);
    }
    return err;
}

void fs_port_deinit(struct superblock *sb)
{
    fs_unmount(sb);
}