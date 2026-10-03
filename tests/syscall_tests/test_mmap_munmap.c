#include "utils.h"

#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20

void _start(void) {
    const int sys_mmap = 9;
    const int sys_munmap = 11;

    // mmap().
    syscall(sys_mmap, 0, 1, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    syscall(sys_mmap, 0, 4096, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    syscall(sys_mmap, 0, 4097, PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void *p = (void *)syscall(sys_mmap, 0, 4096 * 1024, PROT_READ,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

    // munmap().
    syscall(sys_munmap, (unsigned long long)p, 1, 0, 0, 0, 0);
    syscall(sys_munmap, (unsigned long long)p, 4096, 0, 0, 0, 0);
    syscall(sys_munmap, (unsigned long long)p, 4096 * 10, 0, 0, 0, 0);
    syscall(sys_munmap, (unsigned long long)p, 4096 * 1024, 0, 0, 0, 0);
    syscall(sys_munmap, (unsigned long long)p, 4096 * 4096, 0, 0, 0, 0);

    for (;;);
}