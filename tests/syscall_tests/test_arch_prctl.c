#include "utils.h"

#define SYS_arch_prctl 158
#define SYS_exit 60

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_SET_GS 0x1001
#define ARCH_GET_GS 0x1004

#define EFAULT 14
#define EINVAL 22

void exit(int status) { syscall(SYS_exit, status, 0, 0, 0, 0, 0); }

void _start(void) {
    long long ret;
    unsigned long long first = 0, second = 0;

    // Use real guest addresses as FS bases.
    unsigned long long base1 = (unsigned long long)&first;
    unsigned long long base2 = (unsigned long long)&second;

    // ARCH_SET_FS succeeds.
    ret = syscall(SYS_arch_prctl, ARCH_SET_FS, base1, 0, 0, 0, 0);
    if (ret != 0) exit(1);

    // ARCH_GET_FS writes the value set above.
    ret = syscall(SYS_arch_prctl, ARCH_GET_FS, (unsigned long long)&first, 0,
                  0, 0, 0);
    if (ret != 0) exit(2);
    if (first != base1) exit(3);

    // Verify that the FS base actually affects %fs addressing.
    first = 0x1122334455667788ULL;
    unsigned long long seen = 0;
    __asm__ volatile("movq %%fs:0, %0" : "=r"(seen) : : "memory");
    if (seen != 0x1122334455667788ULL) exit(4);

    // Set a different base and read it back.
    ret = syscall(SYS_arch_prctl, ARCH_SET_FS, base2, 0, 0, 0, 0);
    if (ret != 0) exit(5);
    ret = syscall(SYS_arch_prctl, ARCH_GET_FS, (unsigned long long)&second, 0,
                  0, 0, 0);
    if (ret != 0) exit(6);
    if (second != base2) exit(7);

    // Invalid operation codes.
    ret = syscall(SYS_arch_prctl, 0, 0, 0, 0, 0, 0);
    if (ret != -EINVAL) exit(8);
    ret = syscall(SYS_arch_prctl, ARCH_SET_GS, 0, 0, 0, 0, 0);
    if (ret != -EINVAL) exit(9);
    ret = syscall(SYS_arch_prctl, ARCH_GET_GS, 0, 0, 0, 0, 0);
    if (ret != -EINVAL) exit(10);

    // ARCH_GET_FS with an unmapped output pointer.
    ret = syscall(SYS_arch_prctl, ARCH_GET_FS, 0, 0, 0, 0, 0);
    if (ret != -EFAULT) exit(11);

    // Zero is a valid FS base value.
    ret = syscall(SYS_arch_prctl, ARCH_SET_FS, 0, 0, 0, 0, 0);
    if (ret != 0) exit(12);
    second = 0xdeadbeefdeadbeefULL;
    ret = syscall(SYS_arch_prctl, ARCH_GET_FS, (unsigned long long)&second, 0,
                  0, 0, 0);
    if (ret != 0) exit(13);
    if (second != 0) exit(14);

    exit(0);
}
