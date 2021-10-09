// Author: strawberryhacker

#include "stdio.h"
#include "stdlib.h"
#include "assert.h"
#include "utilities.h"
#include "disk.h"
#include "exfat.h"

//--------------------------------------------------------------------------------------------------

static FILE* file;

//--------------------------------------------------------------------------------------------------

static bool disk_read(u32 address, u8* data) {
    if (fseek(file, address * 512, SEEK_SET)) {
        return false;
    }

    return fread(data, BLOCK_SIZE, 1, file) == 1;
}

//--------------------------------------------------------------------------------------------------

static bool disk_write(u32 address, const u8* data) {
    if (fseek(file, address * 512, SEEK_SET)) {
        return false;
    }

    return fwrite(data, BLOCK_SIZE, 1, file) == 1;
}

//--------------------------------------------------------------------------------------------------

int main(int argument_count, const char** arguments) {
    assert(argument_count == 2);

    file = fopen(arguments[1], "r+");
    assert(file);

    DiskOps ops = {
        .read  = disk_read,
        .write = disk_write,
    };

    Disk disk;
    assert(disk_read_partitions(&ops, &disk));
    assert(exfat_mount(&ops, disk.partitions[0].address, "disk") == EXFAT_SUCCESS);

    // Test code goes here.
    Dir dir;
    int status = exfat_open_directory(&dir, "/disk/folder/folder/folder/folder/file.txt");

    return 0;
}
