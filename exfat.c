// Author: strawberryhacker

#include "exfat.h"
#include "stdlib.h"
#include "stdio.h"
#include "array.h"

//--------------------------------------------------------------------------------------------------

#define EXFAT_PATH_DELIMITER  '/'

#define CLUSTER_TO_FAT_ENTRY_SECTOR_SHIFT 7     // Divide by 128
#define CLUSTER_TO_FAT_ENTRY_OFFSET_MASK  0x7F  // Modulo 128

define_array(exfat_array, ExFatArray, ExFat*);

//--------------------------------------------------------------------------------------------------

enum {
    VOLUME_FLAG_ACTIVE_FAT    = 1 << 0,
    VOLUME_FLAG_DIRTY         = 1 << 1,
    VOLUME_FLAG_MEDIA_FAILURE = 1 << 2,
    VOLUME_FLAG_CLEAR_TO_ZERO = 1 << 3,
};

enum {
    FAT_ENTRY_0_VALUE                    = 0xF8FFFFFF,
    FAT_ENTRY_1_VALUE                    = 0xFFFFFFFF,
    FAT_ENTRY_BAD_CLUSTER_VALUE          = 0xFFFFFFF7,
    FAT_ENTRY_END_OF_CLUSTER_CHAIN_VALUE = 0xFFFFFFFF,
};

enum {
    ENTRY_FLAG_USED      = 1 << 7,
    ENTRY_FLAG_FREE      = 0 << 7,
    ENTRY_FLAG_SECONDARY = 1 << 6,
    ENTRY_FLAG_PRIMARY   = 0 << 6,
    ENTRY_FLAG_BENIGN    = 1 << 5,
    ENTRY_FLAG_CRITICAL  = 0 << 5,
};

enum {
    ENTRY_CODE_STREAM       = 0,
    ENTRY_CODE_NAME         = 1,
    ENTRY_CODE_ALLOC_BITMAP = 1,
    ENTRY_CODE_UPCASE_TABLE = 2,
    ENTRY_CODE_VOLUME_LABEL = 3,
    ENTRY_CODE_DIRECTORY    = 5,
};

enum {
    ENTRY_TYPE_END_OF_DIRECTORY = 0,
    ENTRY_TYPE_STREAM           = ENTRY_FLAG_USED | ENTRY_FLAG_CRITICAL | ENTRY_FLAG_SECONDARY | ENTRY_CODE_STREAM,
    ENTRY_TYPE_NAME             = ENTRY_FLAG_USED | ENTRY_FLAG_CRITICAL | ENTRY_FLAG_SECONDARY | ENTRY_CODE_NAME,
    ENTRY_TYPE_ALLOC_BITMAP     = ENTRY_FLAG_USED | ENTRY_FLAG_CRITICAL | ENTRY_FLAG_PRIMARY   | ENTRY_CODE_ALLOC_BITMAP,
    ENTRY_TYPE_UPCASE_TABLE     = ENTRY_FLAG_USED | ENTRY_FLAG_CRITICAL | ENTRY_FLAG_PRIMARY   | ENTRY_CODE_UPCASE_TABLE,
    ENTRY_TYPE_VOLUME_LABEL     = ENTRY_FLAG_USED | ENTRY_FLAG_CRITICAL | ENTRY_FLAG_PRIMARY   | ENTRY_CODE_VOLUME_LABEL,
    ENTRY_TYPE_DIRECTORY        = ENTRY_FLAG_USED | ENTRY_FLAG_CRITICAL | ENTRY_FLAG_PRIMARY   | ENTRY_CODE_DIRECTORY,
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
} ExFatInfo;

typedef struct PACKED {
    u8        jump_boot[3];
    char      name[8];
    u8        must_be_zero[53];
    ExFatInfo info;
    u8        reserved[7];
    u8        boot_code[390];
    u16       signature;
} ExFatHeader;

typedef struct PACKED {
    u8  type;
    u8  flags;
    u8  reserved[18];
    u32 first_cluster;
    u64 length;
} BitmapEntry;

typedef struct PACKED {
    u8 type;
    u8 reserved0[3];
    u32 checksum;
    u8 reserved1[2];
    u32 first_cluster;
    u64 length;
} UpcaseTableEntry;

typedef struct PACKED {
    u8      type;
    u8      label_length;
    Unicode label[11];
    u8      reserved[8];
} VolumeLabelEntry;

typedef struct {
    u8  type;
    u8  secondary_count;
    u16 checksum;
    u16 attributes;
    u8  reserved0[2];
    u32 create_time;
    u32 modified_time;
    u32 access_time;
    u8  create_time_10ms;
    u8  modified_time_10ms;
    u8  create_utc_offset;
    u8  modified_utc_offset;
    u8  accessed_utc_offset;
    u8  reserved1[7];
} DirectoryEntry;

typedef struct {
    u8  type;
    u8  flags;
    u8  reserved0;
    u8  name_length;
    u16 name_checksum;
    u8  reserved1[2];
    u64 valid_length;
    u8  reserved2[4];
    u32 first_cluster;
    u64 length;
} StreamEntry;

typedef struct {
    u8      type;
    u8      flags;
    Unicode name[NAME_ENTRY_CHARACTERS];
} NameEntry;

typedef union {
    u8               type;
    BitmapEntry      bitmap;
    UpcaseTableEntry upcase_table;
    VolumeLabelEntry volume_label;
    DirectoryEntry   directory;
    StreamEntry      stream;
    NameEntry        name;
} Entry;

struct ExFat {
    DiskOps ops;

    String mountpoint;
    char mountpoint_buffer[MOUNTPOINT_NAME_SIZE];

    u32 volume_address_on_disk;
    ExFatInfo info;

    u32 cluster_heap_address;
    u32 fat_table_address;

    u32 cluster_offset_mask;
    u32 cluster_size;
};

typedef struct {
    u32 window_address;
    u32 window_index;
} SavedLocation;

//--------------------------------------------------------------------------------------------------

static ExFatArray exfats;

static const u16 invalid_filename_characters[] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000A, 0x000B, 0x000C, 0x000D, 0x000E, 0x000F,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x001F,
    0x0022, 0x002A, 0x002F, 0x003A, 0x003C, 0x003E, 0x003F, 0x005C,
    0x007C,
};

//--------------------------------------------------------------------------------------------------

// @Cleanup: Remove.
static void print_header(ExFatHeader* header) {
    printf("\n");
    printf("Partition offset           :: %ld\n", header->info.partition_offset        );
    printf("Volume length              :: %ld\n", header->info.volume_length           );
    printf("Fat offset                 :: %d\n", header->info.fat_offset               );
    printf("Fat length                 :: %d\n", header->info.fat_length               );
    printf("Cluster heap offset        :: %d\n", header->info.cluster_heap_offset      );
    printf("Cluster count              :: %d\n", header->info.cluster_count            );
    printf("Root cluster               :: %d\n", header->info.root_cluster             );
    printf("Serial number              :: %d\n", header->info.serial_number            );
    printf("Revision                   :: %d\n", header->info.revision                 );
    printf("Volume flags               :: %d\n", header->info.volume_flags             );
    printf("Bytes per sector_shift     :: %d\n", header->info.bytes_per_sector_shift   );
    printf("Sectors per cluster shift  :: %d\n", header->info.sectors_per_cluster_shift);
    printf("Fat count                  :: %d\n", header->info.fat_count                );
    printf("Drive select               :: %d\n", header->info.drive_select             );
    printf("Percent in use             :: %d\n", header->info.percent_in_use           );
    printf("\n");
}

//--------------------------------------------------------------------------------------------------

// @Cleanup: Remove.
static void print_window_location(File* file) {
    printf("offset  :: %d\n", file->window_index);
    printf("address :: %d\n", file->window_address);
}

//--------------------------------------------------------------------------------------------------

// @Cleanup: Remove.
static void print_block(const void* data) {
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

// @Cleanup: Remove.
static void print_entry(const void* data, bool colorize) {
    if (colorize) {
        printf("\033[32m");
    }

    printf("Entry:\n");
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 16; j++) {
            printf("%02x ", ((u8 *)data)[i * 16 + j]);
        }
        for (int j = 0; j < 16; j++) {
            char c = ((u8 *)data)[i * 16 + j];
            if (c < 32 || c > 126) {
                c = '.';
            }
            printf("%c", c);
        }
        printf("\n");
    }

    for (int i = 0; i < 32;) {
        if ((++i % 16) == 0) {
            printf("\n");
        }
    }
    printf("\033[0m");
    printf("\n");
}

//--------------------------------------------------------------------------------------------------

static void memory_copy(const void* source, void* dest, int size) {
    for (int i = 0; i < size; i++) {
        ((u8 *)dest)[i] = ((const u8 *)source)[i];
    }
}

//--------------------------------------------------------------------------------------------------

static String convert_to_string(char* data) {
    int i;
    for (i = 0; data[i]; i++);

    String string;
    string.length = i;
    string.text = data;
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

static int convert_to_unicode(char* string, Unicode* unicode, int length) {
    int i;
    for (i = 0; string[i] && i < length; i++) {
         unicode[i] = (Unicode)string[i];
    }

    return i;
}

//--------------------------------------------------------------------------------------------------

static bool compare_unicode_filename(char* ascii, Unicode* unicode, int length) {
    for (int i = 0; i < length; i++) {
        if (ascii[i] != (char)unicode[i]) {
            return false;
        }
    }

    return true;
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

static ExFat* get_volume_from_path(String* path) {
    String mountpoint;

    // The first subpath is the mountpoint.
    if (get_next_valid_subpath(path, &mountpoint) == false) {
        return 0;
    }

    for (int i = 0; i < exfats.count; i++) {
        if (string_compare(mountpoint, exfats.items[i]->mountpoint)) {
            return exfats.items[i];
        }
    }

    return 0;
}

//--------------------------------------------------------------------------------------------------

static u32 cluster_to_address(ExFat* exfat, u32 cluster) {
    return exfat->cluster_heap_address + ((cluster - 2) << exfat->info.sectors_per_cluster_shift);
}

//--------------------------------------------------------------------------------------------------

static u32 address_to_cluster(ExFat* exfat, u32 address) {
    return ((address - exfat->cluster_heap_address) >> exfat->info.sectors_per_cluster_shift) + 2;
}

//--------------------------------------------------------------------------------------------------

static int set_window_address(File* file, u32 new_address) {
    if (file->window_valid && file->window_address == new_address) {
        return EXFAT_OK;
    }

    if (file->window_valid && file->window_dirty) {
        if (file->exfat->ops.write(file->window_address, file->window) == false) {
            return EXFAT_DISK_ERROR;
        }

        file->window_dirty = false;
    }

    if (file->exfat->ops.read(new_address, file->window) == false) {
        return EXFAT_DISK_ERROR;
    }

    file->window_address = new_address;
    file->window_valid = true;

    return EXFAT_OK;
}

//--------------------------------------------------------------------------------------------------

static int sync_window(File* file) {
    if (file->window_valid && file->window_dirty) {
        if (file->exfat->ops.write(file->window_address, file->window) == false) {
            return EXFAT_DISK_ERROR;
        }

        file->window_dirty = false;
    }

    return EXFAT_OK;
}

//--------------------------------------------------------------------------------------------------

static int cache_window(File* file) {
    file->window_valid = true;

    if (file->exfat->ops.read(file->window_address, file->window) == false) {
        return EXFAT_DISK_ERROR;
    }

    return EXFAT_OK;
}

//--------------------------------------------------------------------------------------------------

static inline void* get_window_pointer(File* file) {
    return &file->window[file->window_index];
}

//--------------------------------------------------------------------------------------------------

static int go_to_root_directory(File* file) {
    file->window_index = 0;
    file->window_dirty = false;

    return set_window_address(file, cluster_to_address(file->exfat, file->exfat->info.root_cluster));
}

//--------------------------------------------------------------------------------------------------

static int find_volume_and_rewind_to_root_directory(File* file, String* path) {
    ExFat* volume = get_volume_from_path(path);

    if (volume == 0) {
        return EXFAT_WRONG_MOUNTPOINT_IN_PATH;
    }

    // Initialize the exFAT structure.
    file->exfat = volume;
    file->window_valid = false;

    return go_to_root_directory(file);
}

//--------------------------------------------------------------------------------------------------

static void save_window_location(File* file, SavedLocation* location) {
    location->window_index = file->window_index;
    location->window_address = file->window_address;
}

//--------------------------------------------------------------------------------------------------

static int restore_window_location(File* file, SavedLocation* location) {
    file->window_index = location->window_index;
    return set_window_address(file, location->window_address);
}

//--------------------------------------------------------------------------------------------------

static int increment_directory_offset(File* file, u64 increment) {
    int status;

    u32 cluster_size = file->exfat->cluster_size;
    u32 current_cluster = address_to_cluster(file->exfat, file->window_address);

    // Increment will now hold the number of bytes to jump relative to the start of the current cluster.
    increment += file->window_index + ((file->window_address & file->exfat->cluster_offset_mask) * BLOCK_SIZE);

    while (increment >= cluster_size) {
        u32 fat_sector = current_cluster >> CLUSTER_TO_FAT_ENTRY_SECTOR_SHIFT;
        u32 fat_offset = current_cluster & CLUSTER_TO_FAT_ENTRY_OFFSET_MASK;

        status = set_window_address(file, file->exfat->fat_table_address + fat_sector);
        if (status) return status;

        u32 next_cluster = ((u32 *)file->window)[fat_offset];

        if (next_cluster == FAT_ENTRY_BAD_CLUSTER_VALUE) {
            return EXFAT_BAD_CLUSTER;
        }

        if (next_cluster == FAT_ENTRY_END_OF_CLUSTER_CHAIN_VALUE) {
            return EXFAT_END_OF_CLUSTER_CHAIN;
        }

        if (next_cluster < 2) {
            return EXFAT_FREE_CLUSTER;
        }

        current_cluster = next_cluster;
        increment -= cluster_size;
    }

    u32 new_address = cluster_to_address(file->exfat, current_cluster) + (u32)(increment >> file->exfat->info.bytes_per_sector_shift);
    file->window_index = increment & (BLOCK_SIZE - 1);
    return set_window_address(file, new_address);
}

//--------------------------------------------------------------------------------------------------

static int skip_directory_entries(File* file, int count) {
    return increment_directory_offset(file, count * sizeof(Entry));
}

//--------------------------------------------------------------------------------------------------

static int move_window_to_primary_entry(u8 entry_type, File* file) {
    while (1) {
        Entry* entry = get_window_pointer(file);

        if (entry->type == entry_type) {
            return EXFAT_OK;
        }

        if (entry->type == ENTRY_TYPE_END_OF_DIRECTORY) {
            return EXFAT_END_OF_FILE;
        }

        int status = increment_directory_offset(file, sizeof(Entry));
        if (status < 0) return status;
    }
}

//--------------------------------------------------------------------------------------------------

static u16 compute_entry_checksum(u16 checksum, u8* entry, bool is_first) {
    for (int i = 0; i < sizeof(Entry); i++) {
        if (is_first && (i == 2 || i == 3)) {
            continue;
        }

        checksum = ((checksum & 1) ? 0x8000 : 0) + (checksum >> 1) + (u16)entry[i];
    }

    return checksum;
}

//--------------------------------------------------------------------------------------------------

// @Incomplete: We should do more tests here, including checking the state of the file system.
static int verify_exfat_header(ExFatHeader* header) {
    if (header->signature != 0xAA55) {
        return EXFAT_BAD_HEADER_SIGNATURE;
    }

    const char* exfat_string = "EXFAT   ";

    for (int i = 0; i < sizeof(header->name); i++) {
        if (exfat_string[i] != header->name[i]) {
            return EXFAT_NO_EXFAT_VOLUME;
        }
    }

    return EXFAT_OK;
}

//--------------------------------------------------------------------------------------------------

static int find_file_in_current_directory(File* file, String* filename) {
    SavedLocation saved_location;

    while (1) {
        int status = move_window_to_primary_entry(ENTRY_TYPE_DIRECTORY, file);
        if (status) return status;

        // Depending on the file name length, the entry chain may span multiple sectors.
        // We are only interested in the location of the first entry.
        save_window_location(file, &saved_location);

        DirectoryEntry* dir_entry = get_window_pointer(file);

        int secondary_count = dir_entry->secondary_count;
        u16 checksum = dir_entry->checksum;
        u16 attributes = dir_entry->attributes;

        if (dir_entry->secondary_count < 2) {
            return EXFAT_WRONG_SECONDARY_ENTRY_COUNT;
        }

        status = skip_directory_entries(file, 1);
        if (status) return status;

        StreamEntry* stream = get_window_pointer(file);

        u8 name_length = stream->name_length;
        u16 name_checksum = stream->name_checksum;

        if (name_length != filename->length) {
            continue;
        }

        status = skip_directory_entries(file, 1);
        if (status) return status;

        // Skip the stream entry.
        secondary_count--;

        char* name_pointer = filename->text;
        bool match = true;

        // Check if the name matches.
        while (secondary_count--) {
            NameEntry* entry = get_window_pointer(file);

            if (entry->type != ENTRY_TYPE_NAME) {
                return EXFAT_NAME_ENTRY_DOES_NOT_EXIST;
            }

            int length = limit(name_length, NAME_ENTRY_CHARACTERS);

            if (compare_unicode_filename(name_pointer, entry->name, length) == false) {
                match = false;
                break;
            }

            status = skip_directory_entries(file, 1);
            if (status) {
                return status;
            }

            name_length -= length;
            name_pointer += length;
        }

        if (match) {
            return restore_window_location(file, &saved_location);
        }

        skip_directory_entries(file, secondary_count);
    }
}

//--------------------------------------------------------------------------------------------------

static int follow_path(File* file, String* path, bool only_directory) {
    int status;

    // @Hmm: do we need to copy the path?
    String path_copy = *path;
    String subpath;

    status = find_volume_and_rewind_to_root_directory(file, &path_copy);
    if (status) return status;

    while (1) {
        if (get_next_valid_subpath(&path_copy, &subpath) == false) {
            return EXFAT_OK;
        }

        status = find_file_in_current_directory(file, &subpath);
        if (status) return status;

        DirectoryEntry* dir_entry = get_window_pointer(file);

        if (only_directory && (dir_entry->attributes & FILE_ATTRIBUTES_DIRECTORY) == 0) {
            return EXFAT_ATTRIBUTE_ERROR;
        }

        status = skip_directory_entries(file, 1);
        if (status) return status;

        StreamEntry* stream = get_window_pointer(file);

        // This make it easy to go back to the beginning of a file, and implement relative paths.
        file->parent_file_address = file->file_address;
        file->file_address = cluster_to_address(file->exfat, stream->first_cluster);

        file->file_offset = 0;
        file->file_length = stream->length;
        file->valid_length = stream->valid_length;
        file->file_cluster = stream->first_cluster;

        file->window_index = 0;
        status = set_window_address(file, cluster_to_address(file->exfat, stream->first_cluster));
        if (status) return status;
    }
}

//--------------------------------------------------------------------------------------------------

static void convert_to_timestamp(Timestamp* timestamp, u32 time, u32 time_10ms, u8 utc) {
    timestamp->millisecond = 10 * time_10ms;
    timestamp->second      = ((time >> 0 ) & 0b11111) * 2;
    timestamp->minute      = ((time >> 5 ) & 0b111111);
    timestamp->hour        = ((time >> 11) & 0b11111);
    timestamp->day         = ((time >> 16) & 0b11111);
    timestamp->month       = ((time >> 21) & 0b1111) - 1;
    timestamp->year        = ((time >> 25) & 0b1111111) + 1980;
    timestamp->utc         = utc;

    timestamp->millisecond = limit(timestamp->millisecond, 0);
    timestamp->second      = limit(timestamp->second, 60);
    timestamp->minute      = limit(timestamp->minute, 60);
    timestamp->hour        = limit(timestamp->hour  , 24);
    timestamp->day         = limit(timestamp->day   , 31);
    timestamp->month       = limit(timestamp->month , 12);
}

//--------------------------------------------------------------------------------------------------

void exfat_init() {
    exfat_array_init(&exfats, 8);
}

//--------------------------------------------------------------------------------------------------

int exfat_mount(DiskOps* ops, u32 address, char* mountpoint) {
    u8 data[BLOCK_SIZE];

    if (ops->read(address, data) == false) {
        return EXFAT_DISK_ERROR;
    }

    ExFatHeader* header = (ExFatHeader *)data;
    int status = verify_exfat_header(header);
    if (status) return status;

    ExFat* exfat = malloc(sizeof(ExFat));

    // Save the mountpoint.
    int i;
    for (i = 0; mountpoint[i] && i < MOUNTPOINT_NAME_SIZE - 1; i++) {
        exfat->mountpoint_buffer[i] = mountpoint[i];
    }

    if (mountpoint[i]) {
        return EXFAT_MOUNTPOINT_ERROR;
    }

    exfat->mountpoint_buffer[i] = 0;
    exfat->mountpoint = convert_to_string(exfat->mountpoint_buffer);

    // Save info about the file system.
    exfat->ops                    = *ops;
    exfat->volume_address_on_disk = address;
    exfat->info                   = header->info;
    exfat->cluster_heap_address   = exfat->volume_address_on_disk + exfat->info.cluster_heap_offset;
    exfat->fat_table_address      = exfat->volume_address_on_disk + exfat->info.fat_offset + ((exfat->info.volume_flags & VOLUME_FLAG_ACTIVE_FAT) ? exfat->info.fat_length : 0);
    exfat->cluster_offset_mask    = (1 << exfat->info.sectors_per_cluster_shift) - 1;
    exfat->cluster_size           = BLOCK_SIZE << exfat->info.sectors_per_cluster_shift;

    exfat_array_append(&exfats, exfat);
    return EXFAT_OK;
}

//--------------------------------------------------------------------------------------------------

// The volume label must be at least 12 bytes.
int exfat_get_volume_label(File* file, char* mountpoint, char* volume_label) {
    String path = convert_to_string(mountpoint);
    find_volume_and_rewind_to_root_directory(file, &path);

    int status = move_window_to_primary_entry(ENTRY_TYPE_VOLUME_LABEL, file);
    if (status < 0) return status;

    VolumeLabelEntry* entry = get_window_pointer(file);
    int length = limit(entry->label_length, sizeof(entry->label) / sizeof(Unicode));

    int i;
    for (i = 0; i < length; i++) {
        volume_label[i] = (char)entry->label[i];
    }

    volume_label[i] = 0;
    return EXFAT_OK;
}

//--------------------------------------------------------------------------------------------------

int exfat_set_volume_label(File* file, char* mountpoint, char* volume_label) {
    String path = convert_to_string(mountpoint);
    find_volume_and_rewind_to_root_directory(file, &path);

    int status = move_window_to_primary_entry(ENTRY_TYPE_VOLUME_LABEL, file);
    if (status < 0) return status;

    VolumeLabelEntry* entry = get_window_pointer(file);

    entry->label_length = convert_to_unicode(volume_label, entry->label, sizeof(entry->label) / sizeof(Unicode));
    file->window_dirty = true;

    sync_window(file);

    return EXFAT_OK;
}

//--------------------------------------------------------------------------------------------------

int exfat_open_directory(File* file, char* path) {
    String input_path = convert_to_string(path);
    return follow_path(file, &input_path, true);
}

//--------------------------------------------------------------------------------------------------

int exfat_read_directory(File* file, FileInfo* info) {
    int status = move_window_to_primary_entry(ENTRY_TYPE_DIRECTORY, file);
    if (status) return status;

    DirectoryEntry* dir_entry = get_window_pointer(file);

    info->attributes = dir_entry->attributes;
    convert_to_timestamp(&info->create_time, dir_entry->create_time, dir_entry->create_time_10ms, dir_entry->create_utc_offset);
    convert_to_timestamp(&info->access_time, dir_entry->access_time, 0, dir_entry->accessed_utc_offset);
    convert_to_timestamp(&info->modified_time, dir_entry->modified_time, dir_entry->modified_time_10ms, dir_entry->modified_utc_offset);

    int secondary_count = dir_entry->secondary_count;

    skip_directory_entries(file, 1);

    StreamEntry* stream_entry = get_window_pointer(file);

    if (stream_entry->type != ENTRY_TYPE_STREAM) {
        return EXFAT_DIRECTORY_ENTRY_ERROR;
    }

    info->length = stream_entry->length;

    int name_length = stream_entry->name_length;

    // Skip the stream entry.
    skip_directory_entries(file, 1);
    secondary_count--;

    char* name_buffer = info->filename;

    while (secondary_count) {
        if (name_length == 0) {
            return EXFAT_DIRECTORY_ENTRY_ERROR;
        }

        NameEntry* name_entry = get_window_pointer(file);

        if (name_entry->type != ENTRY_TYPE_NAME) {
            return EXFAT_DIRECTORY_ENTRY_ERROR;
        }

        int size = limit(name_length, NAME_ENTRY_CHARACTERS);

        for (int i = 0; i < size; i++) {
            name_buffer[i] = (char)name_entry->name[i];
        }

        name_length -= size;
        name_buffer += size;

        skip_directory_entries(file, 1);
        secondary_count--;
    }

    name_buffer[0] = 0;

    if (secondary_count) {
        skip_directory_entries(file, secondary_count);
        return EXFAT_DIRECTORY_ENTRY_ERROR;
    }

    return EXFAT_OK;
}

//--------------------------------------------------------------------------------------------------

int exfat_open_file(File* file, char* path) {
    String input_path = convert_to_string(path);
    return follow_path(file, &input_path, false);
}

//--------------------------------------------------------------------------------------------------

int exfat_file_read(File* file, void* data, int size, int* bytes_written) {
    int written = 0;
    int total_size = limit(size, file->file_length - file->file_offset);
    u8* pointer = data;

    while (total_size) {
        size = limit(total_size, BLOCK_SIZE - file->window_index);

        int status = cache_window(file);
        if (status) return status;

        memory_copy(get_window_pointer(file), pointer, size);

        status = increment_directory_offset(file, size);
        if (status) {
            return status;
        }

        total_size -= size;
        pointer += size;
        written += size;
        file->file_offset += size;
    }

    *bytes_written = written;
    return EXFAT_OK;
}

//--------------------------------------------------------------------------------------------------

int exfat_set_file_offset(File* file, u64 offset) {
    if (offset >= file->file_length) {
        return EXFAT_FILE_OFFSET_OUT_OF_RANGE;
    }

    // Rewind.
    file->window_address = cluster_to_address(file->exfat, file->file_cluster);
    file->window_index = 0;

    return increment_directory_offset(file, offset);
}

//--------------------------------------------------------------------------------------------------

int exfat_flush(File* file) {
    return sync_window(file);
}
