// Author: strawberryhacker

#include "exfat.h"
#include "stdlib.h"
#include "stdio.h"

//--------------------------------------------------------------------------------------------------

#define EXFAT_SIGNATURE       0xAA55
#define EXFAT_PATH_DELIMITER  '/'

//--------------------------------------------------------------------------------------------------

enum {
    VOLUME_FLAG_ACTIVE_FAT    = 1 << 0,
    VOLUME_FLAG_DIRTY         = 1 << 1,
    VOLUME_FLAG_MEDIA_FAILURE = 1 << 2,
    VOLUME_FLAG_CLEAR_TO_ZERO = 1 << 3,
};

//--------------------------------------------------------------------------------------------------

static Exfat* global_exfat;

//--------------------------------------------------------------------------------------------------

// @Cleanup: Remove.
static void print_header(ExfatHeader* header) {
    printf("\n");
    printf("partition_offset           :: %ld\n", header->info.partition_offset        );
    printf("volume_length              :: %ld\n", header->info.volume_length           );
    printf("fat_offset                 :: %d\n", header->info.fat_offset               );
    printf("fat_length                 :: %d\n", header->info.fat_length               );
    printf("cluster_heap_offset        :: %d\n", header->info.cluster_heap_offset      );
    printf("cluster_count              :: %d\n", header->info.cluster_count            );
    printf("root_cluster               :: %d\n", header->info.root_cluster             );
    printf("serial_number              :: %d\n", header->info.serial_number            );
    printf("revision                   :: %d\n", header->info.revision                 );
    printf("volume_flags               :: %d\n", header->info.volume_flags             );
    printf("bytes_per_sector_shift     :: %d\n", header->info.bytes_per_sector_shift   );
    printf("sectors_per_cluster_shift  :: %d\n", header->info.sectors_per_cluster_shift);
    printf("fat_count                  :: %d\n", header->info.fat_count                );
    printf("drive_select               :: %d\n", header->info.drive_select             );
    printf("percent_in_use             :: %d\n", header->info.percent_in_use           );
    printf("\n");
}

//--------------------------------------------------------------------------------------------------

// @Cleanup: Remove.
void print_block(const void* data) {
    const u8* block = data;

    for (int i = 0; i < 512 / 16; i++) {
        printf("0x%08x  ", i * 16);

        for (int j = 0; j < 16;) {
            printf("%02x ", block[i * 16 + j]);
            if ((++j % 4) == 0) {
                printf(" ");
            }
        }

        for (int j = 0; j < 16; j++) {
            char c = block[i * 16 + j];

            if (c < 32 || c > 126) {
                c = '.';
            }

            printf("%c", c);
        }
        printf("\n");
    }
}

//--------------------------------------------------------------------------------------------------

static String convert_to_string(char* data) {
    int i;
    for (i = 0; data[i]; i++);

    String string;
    string.text = data; 
    string.length = i;

    return string;
}

//--------------------------------------------------------------------------------------------------

static bool string_compare(String string1, String string2) {
    if (string1.length != string2.length) {
        return false;
    }

    for (int i = 0; i < string1.length; i++) {
        if (string1.text[i] != string2.text[i]) {
            return false;
        }
    }

    return true;
}

//--------------------------------------------------------------------------------------------------

static int exfat_header_is_valid(ExfatHeader* header) {
    if (header->signature != EXFAT_SIGNATURE) {
        return EXFAT_BAD_HEADER_SIGNATURE;
    }

    const char* exfat_string = "EXFAT   ";

    for (int i = 0; i < sizeof(header->name); i++) {
        if (exfat_string[i] != header->name[i]) {
            return EXFAT_NO_EXFAT_VOLUME;
        }
    }

    return EXFAT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

int exfat_mount(DiskOps* ops, u32 address, const char* mountpoint) {
    u8 data[BLOCK_SIZE];

    if (ops->read(address, data) == false) {
        return EXFAT_DISK_ERROR;
    }

    ExfatHeader* header = (ExfatHeader *)data;
    int status = exfat_header_is_valid(header);
    if (status) {
        return status;
    }

    // Make a new exFAT file system object.
    Exfat* exfat = malloc(sizeof(Exfat));

    exfat->read = ops->read;
    exfat->write = ops->write;
    exfat->volume_address = address;
    exfat->info = header->info;

    int i;
    for (i = 0; mountpoint[i] && i < MOUNTPOINT_NAME_SIZE - 1; i++) {
        exfat->mountpoint_buffer[i] = mountpoint[i];
    }

    exfat->mountpoint_buffer[i] = 0;
    exfat->mountpoint = convert_to_string(exfat->mountpoint_buffer);

    // @Debug
    print_header(header);

    // @Todo: Replace with list.
    global_exfat = exfat;

    return EXFAT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

static bool get_next_subpath(String* path, String* subpath) {
    if (path->length == 0) {
        return false;
    }

    int i;
    for (i = 0; path->text[i] != EXFAT_PATH_DELIMITER && i < path->length; i++);

    subpath->length = i;
    subpath->text = path->text;

    // Skip the path delimiter if possible.
    if (i < path->length) {
        i++;
    }

    path->length -= i;
    path->text += i;

    return true;
}

//--------------------------------------------------------------------------------------------------

static bool get_next_valid_subpath(String* path, String* subpath) {
    bool status;

    do {
        status = get_next_subpath(path, subpath);
    } while (status == true && subpath->length == 0);

    return status;
}

//--------------------------------------------------------------------------------------------------

static int follow_path(Exfat* exfat, String* path) {
    // Not implemented.
    return EXFAT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

static Exfat* get_exfat_volume(String* path) {
    String mountpoint;

    if (get_next_valid_subpath(path, &mountpoint) == false) {
        return 0;
    }

    // @Incomplete: Replace with list.
    for (int i = 0; i < 1; i++) {
        if (string_compare(mountpoint, global_exfat->mountpoint)) {
            return global_exfat;
        }
    }

    return 0;
}

//--------------------------------------------------------------------------------------------------

static u32 cluster_to_sector(Exfat* exfat, u32 cluster) {
    return exfat->volume_address + exfat->info.cluster_heap_offset + ((cluster - 2) << exfat->info.sectors_per_cluster_shift);
}

//--------------------------------------------------------------------------------------------------

int exfat_open_directory(Dir* dir, char* path) {
    String subpath;
    String input_path = convert_to_string(path);

    Exfat* exfat = get_exfat_volume(&input_path);

    if (exfat == 0) {
        return EXFAT_MOUNTPOINT_ERROR;
    }

    while (1) {
        bool status = get_next_valid_subpath(&input_path, &subpath);

        if (status == false) {
            break;
        }

        printf("Subpath: %.*s\n", subpath.length, subpath.text);
    }
    printf("\n");

    
    u8 data[BLOCK_SIZE];
    if (exfat->read(cluster_to_sector(exfat, exfat->info.root_cluster), data)) {
        print_block(data);
    }

    return EXFAT_SUCCESS;
}
