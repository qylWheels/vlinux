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
    syscall(sys_brk, (unsigned long long)brk + 4096, 0, 0, 0, 0, 0);

    // mprotect() tests.
    syscall(sys_mprotect, (unsigned long long)brk, 4096, PROT_READ, 0, 0, 0);

    for (;;);
}
