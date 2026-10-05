#include "utils.h"

int fn(void *arg) { for (;;); }

void _start(void) {
    const int sys_clone = 56;

    char stack[1024];

    syscall(sys_clone, (unsigned long long)fn, (unsigned long long)stack, 0, 0,
            0, 0);

    for (;;);

    return;
}
