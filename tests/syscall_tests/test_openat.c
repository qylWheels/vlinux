#include "utils.h"

void _start(void) {
    const int sys_openat = 257;
    const int O_CREAT = 0100;
    const int O_RDONLY = 00;
    const int O_WRONLY = 01;

    const char *root = "/";
    const char *path = "hello";
    int root_fd =
        syscall(sys_openat, 0, (unsigned long long)root, O_RDONLY, 0, 0, 0);
    int path_fd = syscall(sys_openat, root_fd, (unsigned long long)path,
                          O_CREAT | O_WRONLY, 0644, 0, 0);

    for (;;);
}
