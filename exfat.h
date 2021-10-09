// Author: strawberryhacker

#ifndef EXFAT_H
#define EXFAT_H

#include "utilities.h"
#include "disk.h"

//--------------------------------------------------------------------------------------------------

#define MOUNTPOINT_NAME_SIZE 64

//--------------------------------------------------------------------------------------------------

enum {
    EXFAT_SUCCESS               =  0,
    EXFAT_DISK_ERROR            = -1,
    EXFAT_BAD_HEADER_SIGNATURE  = -2,
    EXFAT_NO_EXFAT_VOLUME       = -3,
    EXFAT_MOUNTPOINT_ERROR      = -4,
    EXFAT_PATH_ERROR            = -5,
};

//--------------------------------------------------------------------------------------------------

typedef struct PACKED {
    u64 partition_offset;
    u64 volume_length;
    u32 fat_offset;
    u32 fat_length;
    u32 cluster_heap_offset;
    u32 cluster_count;
    u32 root_cluster;
    u32 serial_number;
    u16 revision;
    u16 volume_flags;
    u8  bytes_per_sector_shift;
    u8  sectors_per_cluster_shift;
    u8  fat_count;
    u8  drive_select;
    u8  percent_in_use;
} ExfatInfo;

typedef struct PACKED {
    u8        jump_boot[3];
    char      name[8];
    u8        must_be_zero[53]; 
    ExfatInfo info;
    u8        reserved[7];
    u8        boot_code[390];
    u16       signature;
} ExfatHeader;

typedef struct {
    // Maybe replace with DiskOps.
    bool (*read)(u32 address, u8* data);
    bool (*write)(u32 address, const u8* data);

    u32 volume_address;
    ExfatInfo info;

    String mountpoint;
    char mountpoint_buffer[MOUNTPOINT_NAME_SIZE];
} Exfat;

typedef struct {
    u8 buffer[BLOCK_SIZE];
} Dir;

//--------------------------------------------------------------------------------------------------

int exfat_mount(DiskOps* ops, u32 address, const char* mountpoint);
int exfat_open_directory(Dir* dir, char* path);

#endif
