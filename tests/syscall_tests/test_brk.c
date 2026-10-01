#include "utils.h"

void _start(void) {
    const int sys_brk = 12;

    // Get current break.
    void *brk = (void *)syscall(sys_brk, 0, 0, 0, 0, 0, 0);

    // Grow break.
    syscall(sys_brk, (unsigned long long)brk, 0, 0, 0, 0, 0);
    syscall(sys_brk, (unsigned long long)brk + 1, 0, 0, 0, 0, 0);
    syscall(sys_brk, (unsigned long long)brk + 4095, 0, 0, 0, 0, 0);
    syscall(sys_brk, (unsigned long long)brk + 4096, 0, 0, 0, 0, 0);
    syscall(sys_brk, (unsigned long long)brk + 4096 + 1, 0, 0, 0, 0, 0);

    // Get current break.
    brk = (void *)syscall(sys_brk, 0, 0, 0, 0, 0, 0);

    // Shrink break.
    syscall(sys_brk, (unsigned long long)brk - 1, 0, 0, 0, 0, 0);
    syscall(sys_brk, (unsigned long long)brk - 4095, 0, 0, 0, 0, 0);
    syscall(sys_brk, (unsigned long long)brk - 4096, 0, 0, 0, 0, 0);
    syscall(sys_brk, (unsigned long long)brk - 4096 - 1, 0, 0, 0, 0, 0);

    for (;;);

    return;
}
