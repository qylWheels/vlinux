#include "utils.h"

void _start(void) {
    const int sys_mmap = 9;

    syscall(sys_mmap, 0, 0, 0, 0, 0, 0);

    for (;;);
}