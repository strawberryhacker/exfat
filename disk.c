// Author: strawberryhacker

#include "disk.h"
#include "stdio.h"

//--------------------------------------------------------------------------------------------------

#define MBR_SIGNATURE   0xAA55
#define MBR_ADDRESS     0

//--------------------------------------------------------------------------------------------------

typedef struct PACKED {
    u8 status;
    u8 reserved0[3];
    u8 type;
    u8 reserved1[3];
    u32 address;
    u32 size;
} PartitionEntry;

typedef struct PACKED {
    u8 code[446];
    PartitionEntry entries[PARTITION_COUNT];
    u16 signature;
} MbrHeader;

//--------------------------------------------------------------------------------------------------

bool disk_read_partitions(DiskOps* ops, Disk* disk) {
    u8 data[BLOCK_SIZE];

    if (ops->read(MBR_ADDRESS, data) == false) {
        return false;
    }

    MbrHeader* header = (MbrHeader *)data;

    if (header->signature != MBR_SIGNATURE) {
        return false;
    }

    for (int i = 0; i < PARTITION_COUNT; i++) {
        disk->partitions[i].status  = header->entries[i].status;
        disk->partitions[i].type    = header->entries[i].type;
        disk->partitions[i].address = header->entries[i].address;
        disk->partitions[i].size    = header->entries[i].size;
    }

    return true;
}
