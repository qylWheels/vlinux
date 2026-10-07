#include "utils.h"

void _start(void) {
    const int sys_fork = 57;
    const int sys_exit = 60;
    const int sys_getpid = 39;
    const int sys_getppid = 110;

    int ret = syscall(sys_fork, 0, 0, 0, 0, 0, 0);

    if (ret == 0) {
        // Child process.
        int ppid = syscall(sys_getppid, 0, 0, 0, 0, 0, 0);
        syscall(sys_exit, ppid, 0, 0, 0, 0, 0);
    } else if (ret > 0) {
        // Parent process.
        int pid = syscall(sys_getpid, 0, 0, 0, 0, 0, 0);
        for (;;);  // Ensure ppid not to be adopted by init.
    } else {
        // Error.
        syscall(sys_exit, -1, 0, 0, 0, 0, 0);
    }
}
