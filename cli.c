// Author: strawberryhacker

#include "cli.h"
#include "stdio.h"
#include "stdlib.h"
#include "exfat.h"

//--------------------------------------------------------------------------------------------------

static char input_buffer[1024];
static char path_buffer[1024] = "disk0";
static int path_length = 5;

static File file;
static File dir;
static FileInfo info;

//--------------------------------------------------------------------------------------------------

static bool compare_string(const char* a, const char* b) {
    for (; a[0] && b[0] && (a[0] == b[0]); a++, b++);
    return a[0] == 0 && b[0] == 0;
}

//--------------------------------------------------------------------------------------------------

static bool is_delimiter(char c) {
    return c == ' ' || c == '\n' || c == '\r';
}

//--------------------------------------------------------------------------------------------------

static void add_to_path_buffer(const char* data) {
    if (data[0] == '.' && data[1] == '.') {
        while (path_length && path_buffer[path_length - 1] != '/') {
            path_length--;
        }

        if (path_buffer[path_length - 1] == '/') {
            path_length--;
        }
    }
    else {
        path_buffer[path_length++] = '/';
        while (*data) {
            path_buffer[path_length++] = *data++;
        }
    }

    path_buffer[path_length] = 0;
}

//--------------------------------------------------------------------------------------------------

static void print_directory(File* file) {
    static const char* month_names[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" 
    };

    while (1) {
        int status = exfat_read_directory(file, &info);
        if (status == EXFAT_END_OF_FILE) break;
        if (status) exit(5);

        char* post = "B";
        u64 length = info.length;
        if (length > 1000000000) {
            length /= 1000000;
            post = "MB";
        }
        else if (length > 1000000) {
            length /= 1000;
            post = "KB";
        }

        printf("%8ld %-2s ", length, post);
        printf("%s %-2d, %4d  ", month_names[info.create_time.month], info.create_time.day, info.create_time.year);

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

static void handle_input(char* data) {
    char* strings[100] = {0};
    int string_count = 0;

    while (data[0]) {
        while (is_delimiter(data[0]))
            data++;

        if (data[0] == 0) break;

        strings[string_count++] = data;

        while (data[0] && is_delimiter(data[0]) == false)
            data++;

        if (data[0]) {
            data[0] = 0;
            data++;
        }
    }

    if (strings[0] == 0) {
        return;
    }

    if (compare_string(strings[0], "cd")) {
        if (strings[1] == 0) {
            printf("Wrong argument\n");
            return;
        }

        add_to_path_buffer(strings[1]);
        int status = exfat_open_directory(&dir, path_buffer);

        if (status) {
            add_to_path_buffer("..");
            printf("exFAT error %i\n", status);
        }
    }
    else if (compare_string(strings[0], "list")) {
        int status = exfat_open_directory(&dir, path_buffer);

        if (status) {
            printf("exFAT error %i\n", status);
        }
        print_directory(&dir);
    }
    else if (compare_string(strings[0], "cat")) {
        if (strings[1] == 0) {
            printf("Wrong argument\n");
        }
        add_to_path_buffer(strings[1]);
        exfat_open_file(&file, path_buffer);
        add_to_path_buffer("..");

        print_file(&file);
        printf("\n");
    }
    else if (compare_string(strings[0], "clear")) {
        printf("\033[2J\033[0;0H");
    }
    else {
        printf("Unrecognized option\n");
    }
}

//--------------------------------------------------------------------------------------------------

void cli_task() {    
    printf("\033[36m%.*s \033[0m:: ", path_length, path_buffer);
    void* status = fgets(input_buffer, 1024, stdin);
    if (status == 0) exit(4);

    handle_input(input_buffer);
}
