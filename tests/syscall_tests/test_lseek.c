#include "utils.h"

#define SYS_open 2
#define SYS_lseek 8
#define SYS_exit 60

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define exit(status) syscall(SYS_exit, status, 0, 0, 0, 0, 0)

void _start(void) {
    int fd = syscall(SYS_open, (unsigned long long)"test", O_RDWR | O_CREAT, 0,
                     0, 0, 0);
    if (fd < 0) {
        exit(1);
    }

    int offset = syscall(SYS_lseek, fd, 0x10, SEEK_SET, 0, 0, 0);
    if (offset != 0x10) {
        exit(2);
    }

    offset = syscall(SYS_lseek, fd, 1, SEEK_CUR, 0, 0, 0);
    if (offset != 0x11) {
        exit(3);
    }

    // SEEK_END refers to the end of the file, we didn't write any
    // data to the file, so the end of the file is strictly ZERO.
    offset = syscall(SYS_lseek, fd, 0x10, SEEK_END, 0, 0, 0);
    if (offset != 0x10) {
        exit(4);
    }

    exit(0);
}
