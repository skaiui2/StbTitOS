#ifndef FS_PORT_H
#define FS_PORT_H

#include <stdint.h>
#include "fs.h"

void fs_port_init(void);
int  fs_port_mount(struct superblock *sb);
void fs_port_deinit(struct superblock *sb);

#endif
