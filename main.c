// Author: strawberryhacker

#include "stdio.h"
#include "stdlib.h"
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

    return fread(data, 512, 1, file) == 1;
}

//--------------------------------------------------------------------------------------------------

static bool disk_write(u32 address, const u8* data) {
    if (fseek(file, address * 512, SEEK_SET)) {
        return false;
    }

    return fwrite(data, 512, 1, file) == 1;
}

//--------------------------------------------------------------------------------------------------

int main() {
    file = fopen("test/filesystem", "r+");
    if (file == 0) {
        return 1;
    }

    DiskOps ops = {
        .read  = disk_read,
        .write = disk_write,
    };

    Partitions partitions;

    if (disk_read_partitions(&ops, &partitions) == false) {
        return 1;
    }

    /*
    for (int i = 0; i < PARTITION_COUNT; i++) {
        printf("Partition %d : address = %-6d size = %-6d status = %d type = %d\n",
            i, partitions.index[i].address, partitions.index[i].size, partitions.index[i].status, partitions.index[i].type);
    }
    */

    u32 address = partitions.index[0].address;

    int status = exfat_mount(&ops, address, "disk");
    if (status != EXFAT_SUCCESS) {
        return 1;
    }

    Dir dir;
    status = exfat_open_directory(&dir, "/disk/folder/file.txt");

    /*
    disk
    path
    path
    test
    object
    text.txt
    */

    return 0;
}
