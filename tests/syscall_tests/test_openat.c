#include "utils.h"

void _start(void) {
    const int sys_openat = 257;
    const int O_CREAT = 01;
    const int O_WRONLY = 0100;

    const char *path = "/hello";
    int ret = syscall(sys_openat, 0, (unsigned long long)path,
                      O_CREAT | O_WRONLY, 0644, 0, 0);

    for (;;);
}
