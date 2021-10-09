// Author: strawberryhacker

#ifndef DISK_H
#define DISK_H

#include "utilities.h"

//--------------------------------------------------------------------------------------------------

#define PARTITION_COUNT 4
#define BLOCK_SIZE      512

//--------------------------------------------------------------------------------------------------

typedef struct {
    bool (*read)(u32 address, u8* data);
    bool (*write)(u32 address, const u8* data);
} DiskOps;

typedef struct {
    struct {
        u8 status;
        u8 type;
        u32 address;
        u32 size;
    } index[PARTITION_COUNT];
} Partitions;

//--------------------------------------------------------------------------------------------------

bool disk_read_partitions(DiskOps* ops, Partitions* partitions);

#endif
