// Author: strawberryhacker

#ifndef EXFAT_H
#define EXFAT_H

#include "utilities.h"
#include "disk.h"

//--------------------------------------------------------------------------------------------------

#define MOUNTPOINT_NAME_SIZE    64
#define NAME_ENTRY_CHARACTERS   15
#define MAX_FILE_NAME_LENGTH    257

//--------------------------------------------------------------------------------------------------

enum {
    EXFAT_END_OF_FILE                 =  1,
    EXFAT_OK                          =  0,
    EXFAT_DISK_ERROR                  = -1,
    EXFAT_BAD_HEADER_SIGNATURE        = -2,
    EXFAT_NO_EXFAT_VOLUME             = -3,
    EXFAT_MOUNTPOINT_ERROR            = -4,
    EXFAT_PATH_ERROR                  = -5,
    EXFAT_BAD_CLUSTER                 = -6,
    EXFAT_END_OF_CLUSTER_CHAIN        = -7,
    EXFAT_WRONG_SECONDARY_ENTRY_COUNT = -8,
    EXFAT_NAME_ENTRY_DOES_NOT_EXIST   = -9,
    EXFAT_FREE_CLUSTER                = -10,
    EXFAT_ATTRIBUTE_ERROR             = -11,
    EXFAT_FILE_OFFSET_OUT_OF_RANGE    = -12,
    EXFAT_DIRECTORY_ENTRY_ERROR       = -13,

    EXFAT_WRONG_MOUNTPOINT_IN_PATH    = -14,
};

enum {
    FILE_ATTRIBUTES_READ_ONLY  = 1 << 0,
    FILE_ATTRIBUTES_HIDDEN     = 1 << 1,
    FILE_ATTRIBUTES_SYSTEM     = 1 << 2,
    FILE_ATTRIBUTES_DIRECTORY  = 1 << 4,
    FILE_ATTRIBUTES_ARCHIVE    = 1 << 5,
};

//--------------------------------------------------------------------------------------------------

typedef struct ExFat ExFat;

typedef struct {
    ExFat* exfat;

    u8 window[BLOCK_SIZE];

    bool window_valid;
    bool window_dirty;
    u32  window_address;
    int  window_index;

    // @Cleanup: I do not know if we need to store all these fields.
    u64 file_length;
    u64 file_offset;
    u64 file_address;
    u64 valid_length;
    u32 file_cluster;
    u64 parent_file_address;
} File;

typedef struct {
    u8  millisecond;
    u8  second;
    u8  minute;
    u8  hour;
    u8  day;
    u8  month;
    u16 year;
    u8  utc;
} Timestamp;

typedef struct {
    char filename[MAX_FILE_NAME_LENGTH];

    u16 attributes;
    u64 length;

    Timestamp create_time;
    Timestamp access_time;
    Timestamp modified_time;
} FileInfo;

//--------------------------------------------------------------------------------------------------

void exfat_init();
int exfat_mount(DiskOps* ops, u32 address, char* mountpoint);
int exfat_get_volume_label(File* file, char* mountpoint, char* volume_label);
int exfat_set_volume_label(File* file, char* mountpoint, char* volume_label);
int exfat_open_directory(File* file, char* path);
int exfat_read_directory(File* file, FileInfo* info);
int exfat_open_file(File* file, char* path);
int exfat_file_read(File* file, void* data, int size, int* bytes_written);
int exfat_set_file_offset(File* file, u64 offset);
int exfat_flush(File* file);

#endif
