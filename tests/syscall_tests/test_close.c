#include "utils.h"

#define SYS_open 2
#define SYS_close 3
#define SYS_exit 60

#define O_RDWR 02
#define O_CREAT 0100

#define exit(status) syscall(SYS_exit, status, 0, 0, 0, 0, 0)

void _start(void) {
    int fd = syscall(SYS_open, (unsigned long long)"test", O_RDWR | O_CREAT, 0,
                     0, 0, 0);
    if (fd < 0) {
        exit(1);
    }

    int ret = syscall(SYS_close, fd, 0, 0, 0, 0, 0);
    if (ret != 0) {
        exit(2);
    }

    exit(0);
}
