#include "utils.h"

void _start(void) {
    const int sys_fork = 57;
    const int sys_exit = 60;

    int ret = syscall(sys_fork, 0, 0, 0, 0, 0, 0);

    if (ret == 0) {
        // Child process.
        // Infinite loop, so it will be entrusted to init.
        for (;;);
    } else if (ret > 0) {
        // Parent process.
        // Immediately exit, so the child process will be adopted by init.
        syscall(sys_exit, 42, 0, 0, 0, 0, 0);
    } else {
        // Error.
    }
}
