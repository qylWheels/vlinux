#include "utils.h"

void _start(void) {
    const int sys_mmap = 9;

    syscall(sys_mmap, 0, 0x100, 0, 0, -1, 0);

    for (;;);
}