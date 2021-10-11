// Author: strawberryhacker

#ifndef EXFAT_H
#define EXFAT_H

#include "utilities.h"
#include "disk.h"

//--------------------------------------------------------------------------------------------------

#define MOUNTPOINT_NAME_SIZE 64
#define NAME_ENTRY_CHARACTERS 15
#define MAX_FILE_NAME_LENGTH 257

//--------------------------------------------------------------------------------------------------

enum {
    EXFAT_END_OF_FILE                 =  1,
    EXFAT_SUCCESS                     =  0,
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
};

enum {
    FILE_ATTRIBUTES_READ_ONLY  = 1 << 0,
    FILE_ATTRIBUTES_HIDDEN     = 1 << 1,
    FILE_ATTRIBUTES_SYSTEM     = 1 << 2,
    FILE_ATTRIBUTES_DIRECTORY  = 1 << 4,
    FILE_ATTRIBUTES_ARCHIVE    = 1 << 5,
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
    u8 type;
    u8 flags;
    u8 reserved[18];
    u32 first_cluster;
    u64 length;
} BitmapEntry;

typedef struct {
    u8 type;
    u8 reserved0[3];
    u32 checksum;
    u8 reserved1[2];
    u32 first_cluster;
    u64 length;
} UpcaseEntry;

typedef struct {
    u8 type;
    u8 label_length;
    Unicode label[11];
    u8 reserved[8];
} VolumeLabelEntry;

typedef struct {
    u8 type;
    u8 secondary_count;
    u16 checksum;
    u16 attributes;
    u8 reserved0[2];
    u32 create_time;
    u32 modified_time;
    u32 access_time;
    u8 create_time_10ms;
    u8 modified_time_10ms;
    u8 create_utc_offset;
    u8 modified_utc_offset;
    u8 accessed_utc_offset;
    u8 reserved1[7];
} DirectoryEntry;

typedef struct {
    u8 type;
    u8 flags;
    u8 reserved0;
    u8 name_length;
    u16 name_checksum;
    u8 reserved1[2];
    u64 valid_length;
    u8 reserved2[4];
    u32 first_cluster;
    u64 length;
} StreamEntry;

typedef struct {
    u8 type;
    u8 flags;
    Unicode name[NAME_ENTRY_CHARACTERS];
} NameEntry;

typedef union {
    u8 type;
    BitmapEntry       bitmap;
    UpcaseEntry       upcase;
    VolumeLabelEntry  volume_label;
    DirectoryEntry    directory;
    StreamEntry       stream;
    NameEntry         name;
} Entry;

typedef struct {
    DiskOps ops;
    String mountpoint;
    char mountpoint_buffer[MOUNTPOINT_NAME_SIZE];

    u32 volume_address;
    ExfatInfo info;

    u32 cluster_heap_address;
    u32 fat_table_address;
    
    u32 bytes_per_sector_mask;
    u32 sectors_per_cluster_mask;
    u32 cluster_size;
} Exfat;

typedef struct {
    u8 buffer[BLOCK_SIZE];
    u32 buffer_address;
    bool valid;
    bool dirty;

    u32 cluster;
    u32 address;
    u32 offset;

    u64 file_length;
    u64 file_offset;
    u64 file_address;
    u64 valid_length;
    u32 file_cluster;

    u64 parent_file_address;

    Exfat* exfat;
} Object, Directory, File;

typedef struct {
    u32 cluster;
    u32 address;
    u32 offset;
} SavedLocation;

typedef struct {
    u8 millisecond;
    u8 second;
    u8 minute;
    u8 hour;
    u8 day;
    u8 month;
    u16 year;
    u8 utc;
} Timestamp;

typedef struct {
    char filename[MAX_FILE_NAME_LENGTH];

    u16 attributes;
    u64 length;

    Timestamp create_time;
    Timestamp access_time;
    Timestamp modified_time;
} DirectoryInfo;

//--------------------------------------------------------------------------------------------------

void exfat_init();

int exfat_get_volume_label(Directory* dir, char* path, char* volume_label);
int exfat_set_volume_label(Directory* dir, char* path, char* volume_label);

int exfat_mount(DiskOps* ops, u32 address, char* mountpoint);

int exfat_open_directory(Directory* dir, char* path);
int exfat_read_directory(Directory* dir, DirectoryInfo* info);

int exfat_open_file(File* file, char* path);
int exfat_file_read(File* file, void* data, int size, int* bytes_written);
int exfat_set_file_offset(File* file, u64 offset);
int exfat_flush(File* file);

#endif
