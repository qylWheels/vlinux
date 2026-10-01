#include "utils.h"

const int sys_mprotect = 10;
const int sys_brk = 12;

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

void _start(void) {
    // Get current break.
    void *brk = (void *)syscall(sys_brk, 0, 0, 0, 0, 0, 0);

    // Grow break.
    syscall(sys_brk, (unsigned long long)brk + 4096 * 100, 0, 0, 0, 0, 0);

    // Single page, single protection flag.
    syscall(sys_mprotect, (unsigned long long)brk, 4096, PROT_READ, 0, 0, 0);
    syscall(sys_mprotect, (unsigned long long)brk, 1, PROT_WRITE, 0, 0, 0);
    syscall(sys_mprotect, (unsigned long long)brk, 0, PROT_EXEC, 0, 0, 0);

    // Multiple pages, single protection flag.
    syscall(sys_mprotect, (unsigned long long)brk, 4096 + 1, PROT_READ, 0, 0,
            0);
    syscall(sys_mprotect, (unsigned long long)brk, 4096 * 2, PROT_WRITE, 0, 0,
            0);
    syscall(sys_mprotect, (unsigned long long)brk, 4096 * 100, PROT_EXEC, 0, 0,
            0);

    for (;;);
}
