// Author: strawberryhacker

#include "stdio.h"
#include "stdlib.h"
#include "stdarg.h"
#include "assert.h"
#include "utilities.h"
#include "disk.h"
#include "exfat.h"
#include "cli.h"

//--------------------------------------------------------------------------------------------------

static FILE* filesystem_file;

//--------------------------------------------------------------------------------------------------

static bool disk_read(u32 address, u8* data) {
    if (fseek(filesystem_file, address * BLOCK_SIZE, SEEK_SET)) {
        return false;
    }

    return fread(data, BLOCK_SIZE, 1, filesystem_file) == 1;
}

//--------------------------------------------------------------------------------------------------

static bool disk_write(u32 address, const u8* data) {
    if (fseek(filesystem_file, address * BLOCK_SIZE, SEEK_SET)) {
        return false;
    }

    return fwrite(data, BLOCK_SIZE, 1, filesystem_file) == 1;
}

//--------------------------------------------------------------------------------------------------

static File dir;
static File file;
static FileInfo info;

static const char* month_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" 
};

//--------------------------------------------------------------------------------------------------

static void print_directory(File* file) {
    while (1) {
        int status = exfat_read_directory(file, &info);
        if (status == EXFAT_END_OF_FILE) break;
        if (status) exit(5);

        printf(" > %8ld ", info.length);
        printf("%2d.%s %4d ", info.create_time.day, month_names[info.create_time.month], info.create_time.year);

        if (info.attributes & FILE_ATTRIBUTES_DIRECTORY) {
            printf("\033[46;30m");
        }
        else {
            printf("\033[36;40m");
        }

        printf("%s", info.filename);
        printf("\033[0m\n");
    }
}

//--------------------------------------------------------------------------------------------------

static void print_file(File* file) {
    char data[BLOCK_SIZE];
    int written;

    while (1) {
        int status = exfat_file_read(file, data, BLOCK_SIZE, &written);
        if (status) exit(-status);

        printf("%.*s", written, data);

        if (written < 512) {
            break;
        }
    }
}

//--------------------------------------------------------------------------------------------------

int main(int argument_count, const char** arguments) {
    assert(argument_count == 2);
    filesystem_file = fopen(arguments[1], "r+");
    assert(filesystem_file);
    exfat_init();

    int status;

    DiskOps ops = {
        .read  = disk_read,
        .write = disk_write,
    };

    Disk disk;
    assert(disk_read_partitions(&ops, &disk));
    assert(exfat_mount(&ops, disk.partitions[0].address, "disk0") == EXFAT_OK);

    while (1) {
        cli_task();
    }

    char volume_label[12];
    status = exfat_get_volume_label(&dir, "disk0", volume_label);
    printf("Volume label : %s\n", volume_label);
    status = exfat_set_volume_label(&dir, "disk0", "AWEEE");

    status = exfat_open_directory(&dir, "disk0/");
    if (status) return -status;


    print_directory(&dir);

    status = exfat_open_file(&file, "disk0/source/make/makefile");
    if (status) return -status;

    status = exfat_set_file_offset(&file, 100);
    if (status) return -status;

    print_file(&file);

    return 0;
}
