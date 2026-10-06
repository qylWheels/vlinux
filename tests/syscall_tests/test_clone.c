#include "utils.h"

void _start(void) {
    const int sys_clone = 56;

    char stack[1024];

    syscall(sys_clone, 0, 0, 0, 0, 0, 0);

    for (;;);

    return;
}
