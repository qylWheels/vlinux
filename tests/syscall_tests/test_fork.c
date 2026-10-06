#include "utils.h"

void _start(void) {
    const int sys_fork = 57;

    int ret = syscall(sys_fork, 0, 0, 0, 0, 0, 0);

    if (ret == 0) {
        // Child process.
    } else if (ret > 0) {
        // Parent process.
    } else {
        // Error.
    }

    for (;;);

    return;
}
