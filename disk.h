// Author: strawberryhacker

#ifndef DISK_H
#define DISK_H

#include "utilities.h"

//--------------------------------------------------------------------------------------------------

#define PARTITION_COUNT  4
#define BLOCK_SIZE       512

//--------------------------------------------------------------------------------------------------

typedef struct {
    bool (*read)(u32 address, u8* data);
    bool (*write)(u32 address, const u8* data);
} DiskOps;

typedef struct {
    u8 status;
    u8 type;
    u32 address;
    u32 size;
} Partition;

typedef struct {
    Partition partitions[PARTITION_COUNT];
} Disk;

//--------------------------------------------------------------------------------------------------

bool disk_read_partitions(DiskOps* ops, Disk* disk);

#endif
