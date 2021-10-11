// Author: strawberryhacker

#include "exfat.h"
#include "stdlib.h"
#include "stdio.h"
#include "array.h"

//--------------------------------------------------------------------------------------------------

#define EXFAT_SIGNATURE       0xAA55
#define EXFAT_PATH_DELIMITER  '/'

#define EXFAT_INITIAL_ENTRY_CHECKSUM 0

#define EXFAT_FAT0_ENTRY                 0xF8FFFFFF
#define EXFAT_FAT1_ENTRY                 0xFFFFFFFF
#define EXFAT_TABLE_BAD_CLUSTER          0xFFFFFFF7
#define EXFAT_TABLE_END_OF_CLUSTER_CHAIN 0xFFFFFFFF

#define ENTRY_USED_MASK       (1 << 7)
#define ENTRY_FREE_MASK       (0 << 7)
#define ENTRY_SECONDARY_MASK  (1 << 6)
#define ENTRY_PRIMARY_MASK    (0 << 6)
#define ENTRY_BENIGN_MASK     (1 << 5)
#define ENTRY_CRIICAL_MASK    (0 << 5)

#define CLUSTER_TO_FAT_SECTOR_SHIFT 7
#define CLUSTER_TO_FAT_OFFSET_MASK 0b1111111

#define ENTRY_CODE_STREAM       0
#define ENTRY_CODE_NAME         1
#define ENTRY_CODE_ALLOC_BITMAP 1
#define ENTRY_CODE_UPCASE_TABLE 2
#define ENTRY_CODE_VOLUME_LABEL 3
#define ENTRY_CODE_DIRECTORY    5

//--------------------------------------------------------------------------------------------------

enum {
    VOLUME_FLAG_ACTIVE_FAT    = 1 << 0,
    VOLUME_FLAG_DIRTY         = 1 << 1,
    VOLUME_FLAG_MEDIA_FAILURE = 1 << 2,
    VOLUME_FLAG_CLEAR_TO_ZERO = 1 << 3,
};

enum {
    ENTRY_TYPE_END_OF_DIRECTORY = 0,
    ENTRY_TYPE_STREAM           = ENTRY_USED_MASK | ENTRY_CRIICAL_MASK | ENTRY_SECONDARY_MASK | ENTRY_CODE_STREAM,
    ENTRY_TYPE_NAME             = ENTRY_USED_MASK | ENTRY_CRIICAL_MASK | ENTRY_SECONDARY_MASK | ENTRY_CODE_NAME,
    ENTRY_TYPE_ALLOC_BITMAP     = ENTRY_USED_MASK | ENTRY_CRIICAL_MASK | ENTRY_PRIMARY_MASK   | ENTRY_CODE_ALLOC_BITMAP,
    ENTRY_TYPE_UPCASE_TABLE     = ENTRY_USED_MASK | ENTRY_CRIICAL_MASK | ENTRY_PRIMARY_MASK   | ENTRY_CODE_UPCASE_TABLE,
    ENTRY_TYPE_VOLUME_LABEL     = ENTRY_USED_MASK | ENTRY_CRIICAL_MASK | ENTRY_PRIMARY_MASK   | ENTRY_CODE_VOLUME_LABEL,
    ENTRY_TYPE_DIRECTORY        = ENTRY_USED_MASK | ENTRY_CRIICAL_MASK | ENTRY_PRIMARY_MASK   | ENTRY_CODE_DIRECTORY,
};

//--------------------------------------------------------------------------------------------------

define_array(exfat_array, ExfatArray, Exfat*);
static ExfatArray exfats;

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

// @Remove
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

static String convert_to_string(char* data) {
    int i;
    for (i = 0; data[i]; i++);

    return (String) {
        .text = data,
        .length = i
    };
}

//--------------------------------------------------------------------------------------------------

static void directory_init(Directory* dir, Exfat* exfat) {
    dir->exfat = exfat;
    dir->valid = false;
    dir->dirty = false;
}

//--------------------------------------------------------------------------------------------------

static u32 cluster_to_sector(Exfat* exfat, u32 cluster) {
    return exfat->cluster_heap_address + ((cluster - 2) << exfat->info.sectors_per_cluster_shift);
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

void exfat_init() {
    exfat_array_init(&exfats, 8);
}

//--------------------------------------------------------------------------------------------------

static Exfat* find_exfat_volume(String* path) {
    String mountpoint;

    if (get_next_valid_subpath(path, &mountpoint) == false) {
        return 0;
    }

    // @Incomplete: Replace with list.
    for (int i = 0; i < exfats.count; i++) {
        if (string_compare(mountpoint, exfats.items[i]->mountpoint)) {
            return exfats.items[i];
        }
    }

    return 0;
}

//--------------------------------------------------------------------------------------------------

static u32 shift_to_mask(u32 shift) {
    u32 value = 0;

    for (int i = 0; i < shift; i++) {
        value = value << 1 | 1;
    }

    return value;
}

//--------------------------------------------------------------------------------------------------

int exfat_mount(DiskOps* ops, u32 address, char* mountpoint) {
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

    exfat->ops = *ops;
    exfat->volume_address = address;
    exfat->info = header->info;

    int i;
    for (i = 0; mountpoint[i] && i < MOUNTPOINT_NAME_SIZE - 1; i++) {
        exfat->mountpoint_buffer[i] = mountpoint[i];
    }

    if (mountpoint[i]) {
        return EXFAT_MOUNTPOINT_ERROR;
    }

    exfat->mountpoint_buffer[i] = 0;
    exfat->mountpoint = convert_to_string(exfat->mountpoint_buffer);

    // Compute some addresses for better performance.
    exfat->cluster_heap_address = exfat->volume_address + exfat->info.cluster_heap_offset;
    exfat->fat_table_address = exfat->volume_address + exfat->info.fat_offset + ((exfat->info.volume_flags & VOLUME_FLAG_ACTIVE_FAT) ? exfat->info.fat_length : 0);

    exfat->bytes_per_sector_mask = shift_to_mask(exfat->info.bytes_per_sector_shift);
    exfat->sectors_per_cluster_mask = shift_to_mask(exfat->info.sectors_per_cluster_shift);
    exfat->cluster_size = BLOCK_SIZE << exfat->info.sectors_per_cluster_shift;


    exfat_array_append(&exfats, exfat);
    print_header(header);

    return EXFAT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

static int cache_directory_address_data(Directory* dir) {
    if (dir->valid && dir->buffer_address == dir->address) {
        return EXFAT_SUCCESS;
    }

    // @Incomplete.
    if (dir->dirty) {
        dir->exfat->ops.write(dir->buffer_address, dir->buffer);
    }

    if (dir->exfat->ops.read(dir->address, dir->buffer) == false) {
        return EXFAT_DISK_ERROR;
    }

    dir->buffer_address = dir->address;
    dir->dirty = false;
    dir->valid = true;
    return EXFAT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

static int flush_directory_buffer(Directory* dir) {
    int status = EXFAT_SUCCESS;

    if (dir->valid && dir->dirty) {
        status = dir->exfat->ops.write(dir->address, dir->buffer);
        dir->dirty = false;
    }

    return status;
}

//--------------------------------------------------------------------------------------------------

static void print_current_location(Directory* dir) {
    printf("Offset  :: %d\n", dir->offset);
    printf("Address :: %d\n", dir->address);
    printf("Cluster :: %d\n", dir->cluster);
}

//--------------------------------------------------------------------------------------------------

static int load_single_sector(Directory* dir, u32 address) {
    dir->address = address;
    return cache_directory_address_data(dir);
}

//--------------------------------------------------------------------------------------------------

static int increment_directory_offset(Directory* dir, u64 increment) {
    u32 cluster_size = dir->exfat->cluster_size;
    u32 start_address = dir->address;

    increment += dir->offset + ((dir->address & dir->exfat->sectors_per_cluster_mask) * BLOCK_SIZE);

    while (increment >= cluster_size) {

        u32 fat_sector = dir->cluster >> CLUSTER_TO_FAT_SECTOR_SHIFT;
        u32 fat_offset = dir->cluster & CLUSTER_TO_FAT_OFFSET_MASK;

        int status = load_single_sector(dir, dir->exfat->fat_table_address + fat_sector);
        if (status) {
            return status;
        }

        u32 next_cluster = ((u32 *)dir->buffer)[fat_offset];

        if (next_cluster == EXFAT_TABLE_BAD_CLUSTER) {
            return EXFAT_BAD_CLUSTER;
        }

        if (next_cluster == EXFAT_TABLE_END_OF_CLUSTER_CHAIN) {
            return EXFAT_END_OF_CLUSTER_CHAIN;
        }

        if (next_cluster < 2) {
            return EXFAT_FREE_CLUSTER;
        }

        dir->cluster = next_cluster;
        increment -= cluster_size;
    }

    dir->offset = increment & (BLOCK_SIZE - 1);
    dir->address = cluster_to_sector(dir->exfat, dir->cluster) + (u32)(increment >> dir->exfat->info.bytes_per_sector_shift);
    cache_directory_address_data(dir);
    return EXFAT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

static int move_to_primary_entry(u8 entry_type, Directory* dir) {
    while (1) {
        cache_directory_address_data(dir);

        Entry* entry = (Entry *)&dir->buffer[dir->offset];

        if (entry->type == entry_type) {
            return EXFAT_SUCCESS;
        }

        if (entry->type == ENTRY_TYPE_END_OF_DIRECTORY) {
            return EXFAT_END_OF_FILE;
        }

        int status = increment_directory_offset(dir, sizeof(Entry));
        if (status) {
            return status;
        }
    }
}

//--------------------------------------------------------------------------------------------------

static void save_location(Directory* dir, SavedLocation* location) {
    location->offset = dir->offset;
    location->address = dir->address;
    location->cluster = dir->cluster;
}

//--------------------------------------------------------------------------------------------------

static void restore_location(Directory* dir, SavedLocation* location) {
    dir->address = location->address;
    dir->cluster = location->cluster;
    dir->offset = location->offset;
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

static int skip_directory_entries(Directory* dir, int count) {
    return increment_directory_offset(dir, count * sizeof(Entry));
}

//--------------------------------------------------------------------------------------------------

static int find_file_in_current_directory(Directory* dir, String* filename) {
    SavedLocation saved_location;

    while (1) {
        int status = move_to_primary_entry(ENTRY_TYPE_DIRECTORY, dir);
        if (status) {
            return status;
        }

        // If the directory name matches we return the directory entry. This saves the
        // address of this entry.
        save_location(dir, &saved_location);

        DirectoryEntry* dir_entry = (DirectoryEntry *)&dir->buffer[dir->offset];

        int secondary_count = dir_entry->secondary_count;
        u16 checksum = dir_entry->checksum;
        u16 attributes = dir_entry->attributes;

        if (dir_entry->secondary_count < 2) {
            return EXFAT_WRONG_SECONDARY_ENTRY_COUNT;
        }

        status = skip_directory_entries(dir, 1);
        if (status) {
            return status;
        }

        StreamEntry* stream = (StreamEntry *)&dir->buffer[dir->offset];

        u8 name_length = stream->name_length;
        u16 name_checksum = stream->name_checksum;

        if (name_length != filename->length) {
            continue;
        }

        status = skip_directory_entries(dir, 1);
        if (status) {
            return status;
        }

        // Skip the stream entry.
        secondary_count--;

        char* name_pointer = filename->text;
        bool match = true;

        // Check if the name matches.
        while (secondary_count--) {
            NameEntry* entry = (NameEntry *)&dir->buffer[dir->offset];

            if (entry->type != ENTRY_TYPE_NAME) {
                return EXFAT_NAME_ENTRY_DOES_NOT_EXIST;
            }

            int length = limit(name_length, NAME_ENTRY_CHARACTERS);

            if (compare_unicode_filename(name_pointer, entry->name, length) == false) {
                match = false;
                break;
            }

            status = skip_directory_entries(dir, 1);
            if (status) {
                return status;
            }

            name_length -= length;
            name_pointer += length;
        }

        if (match) {
            restore_location(dir, &saved_location);
            return cache_directory_address_data(dir);
        }

        skip_directory_entries(dir, secondary_count);
    }
}

//--------------------------------------------------------------------------------------------------

static void rewind_to_root_directory(Directory* dir) {
    dir->offset = 0;
    dir->cluster = dir->exfat->info.root_cluster;
    dir->address = cluster_to_sector(dir->exfat, dir->cluster);
    dir->dirty = false;
}

//--------------------------------------------------------------------------------------------------

static int find_volume_and_rewind_to_root_directory(Directory* dir, String* path) {
    Exfat* exfat = find_exfat_volume(path);
    if (exfat == 0) {
        return EXFAT_MOUNTPOINT_ERROR;
    }

    directory_init(dir, exfat);
    rewind_to_root_directory(dir);
    return EXFAT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

static int follow_path(Directory* dir, String* path, bool only_directory) {
    String path_copy = *path;
    String subpath;

    find_volume_and_rewind_to_root_directory(dir, &path_copy);

    while (1) {
        if (get_next_valid_subpath(&path_copy, &subpath) == false) {
            return EXFAT_SUCCESS;
        }

        int status = find_file_in_current_directory(dir, &subpath);
        if (status) {
            return status;
        }

        DirectoryEntry* dir_entry = (DirectoryEntry *)&dir->buffer[dir->offset];

        if (only_directory && (dir_entry->attributes & FILE_ATTRIBUTES_DIRECTORY) == 0) {
            return EXFAT_ATTRIBUTE_ERROR;
        }

        skip_directory_entries(dir, 1);
        cache_directory_address_data(dir);

        StreamEntry* stream = (StreamEntry *)&dir->buffer[dir->offset];

        // This make it easy to go back to the beginning of a file, and implement relative paths.
        dir->parent_file_address = dir->file_address;
        dir->file_address = cluster_to_sector(dir->exfat, stream->first_cluster);

        dir->file_offset = 0;
        dir->file_length = stream->length;
        dir->valid_length = stream->valid_length;
        dir->file_cluster = stream->first_cluster;

        dir->cluster = stream->first_cluster;
        dir->offset = 0;
        dir->address = cluster_to_sector(dir->exfat, dir->cluster);
    }
}

//--------------------------------------------------------------------------------------------------

int exfat_get_volume_label(Directory* dir, char* path, char* volume_label) {
    // Entering the volume.
    String path_copy = convert_to_string(path);
    find_volume_and_rewind_to_root_directory(dir, &path_copy);

    int status = move_to_primary_entry(ENTRY_TYPE_VOLUME_LABEL, dir);
    if (status < 0) return status;

    VolumeLabelEntry* entry = (VolumeLabelEntry *)&dir->buffer[dir->offset];
    int length = limit(entry->label_length, sizeof(entry->label) / sizeof(Unicode));

    int i;
    for (i = 0; i < length; i++) {
        volume_label[i] = (char)entry->label[i];
    }

    volume_label[i] = 0;
    return EXFAT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

static int convert_to_unicode(char* string, Unicode* unicode, int count) {
    int i;
    for (i = 0; string[i] && i < count; i++) {
         unicode[i] = (Unicode)string[i];
    }

    return i;
}

//--------------------------------------------------------------------------------------------------

int exfat_set_volume_label(Directory* dir, char* path, char* volume_label) {
    // Entering the volume.
    String path_copy = convert_to_string(path);
    find_volume_and_rewind_to_root_directory(dir, &path_copy);

    int status = move_to_primary_entry(ENTRY_TYPE_VOLUME_LABEL, dir);
    if (status < 0) return status;

    VolumeLabelEntry* entry = (VolumeLabelEntry *)&dir->buffer[dir->offset];

    entry->label_length = convert_to_unicode(volume_label, entry->label, sizeof(entry->label) / sizeof(Unicode));
    dir->dirty = true;
    
    flush_directory_buffer(dir);
    
    return EXFAT_SUCCESS;
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

int exfat_open_directory(Directory* dir, char* path) {
    String input_path = convert_to_string(path);
    return follow_path(dir, &input_path, true);
}

//--------------------------------------------------------------------------------------------------

int exfat_read_directory(Directory* dir, DirectoryInfo* info) {
    int status = move_to_primary_entry(ENTRY_TYPE_DIRECTORY, dir);
    if (status) return status;

    DirectoryEntry* dir_entry = (DirectoryEntry *)&dir->buffer[dir->offset];

    info->attributes = dir_entry->attributes;
    convert_to_timestamp(&info->create_time, dir_entry->create_time, dir_entry->create_time_10ms, dir_entry->create_utc_offset);
    convert_to_timestamp(&info->access_time, dir_entry->access_time, 0, dir_entry->accessed_utc_offset);
    convert_to_timestamp(&info->modified_time, dir_entry->modified_time, dir_entry->modified_time_10ms, dir_entry->modified_utc_offset);

    int secondary_count = dir_entry->secondary_count;

    skip_directory_entries(dir, 1);

    StreamEntry* stream_entry = (StreamEntry *)&dir->buffer[dir->offset];

    if (stream_entry->type != ENTRY_TYPE_STREAM) {
        return EXFAT_DIRECTORY_ENTRY_ERROR;
    }

    info->length = stream_entry->length;

    int name_length = stream_entry->name_length;

    // Skip the stream entry.
    skip_directory_entries(dir, 1);
    secondary_count--;

    char* name_buffer = info->filename;

    while (secondary_count) {
        if (name_length == 0) {
            return EXFAT_DIRECTORY_ENTRY_ERROR;
        }

        NameEntry* name_entry = (NameEntry *)&dir->buffer[dir->offset];

        if (name_entry->type != ENTRY_TYPE_NAME) {
            return EXFAT_DIRECTORY_ENTRY_ERROR;
        }

        int size = limit(name_length, NAME_ENTRY_CHARACTERS);

        for (int i = 0; i < size; i++) {
            name_buffer[i] = (char)name_entry->name[i];
        }

        name_length -= size;
        name_buffer += size;

        skip_directory_entries(dir, 1);
        secondary_count--;
    }

    name_buffer[0] = 0;

    if (secondary_count) {
        skip_directory_entries(dir, secondary_count);
        return EXFAT_DIRECTORY_ENTRY_ERROR;
    }

    return EXFAT_SUCCESS;
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
        size = limit(total_size, BLOCK_SIZE - file->offset);

        int status = cache_directory_address_data(file);
        if (status) {
            return status;
        }

        memory_copy(&file->buffer[file->offset], pointer, size);

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
    return EXFAT_SUCCESS;
}

//--------------------------------------------------------------------------------------------------

int exfat_set_file_offset(File* file, u64 offset) {
    if (offset >= file->file_length) {
        return EXFAT_FILE_OFFSET_OUT_OF_RANGE;
    }

    // Rewind.
    file->cluster = file->file_cluster;
    file->address = cluster_to_sector(file->exfat, file->cluster);
    file->offset = 0;

    return increment_directory_offset(file, offset);
}

//--------------------------------------------------------------------------------------------------

int exfat_flush(File* file) {
    return flush_directory_buffer(file);
}
