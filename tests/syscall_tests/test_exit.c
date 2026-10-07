#include "utils.h"

void _start(void) {
    const int sys_exit = 60;

    syscall(sys_exit, 0, 0, 0, 0, 0, 0);
}
