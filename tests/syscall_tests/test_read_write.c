#include "utils.h"

#define SYS_read 0
#define SYS_write 1
#define SYS_open 2
#define SYS_lseek 8
#define SYS_exit 60

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100

#define SEEK_SET 0
#define SEEK_CUR 1

#define EBADF 9

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void exit(int status) { syscall(SYS_exit, status, 0, 0, 0, 0, 0); }

void _start(void) {
    char buf[16] = {0};
    long long ret;

    int fd = syscall(SYS_open, (unsigned long long)"read_write",
                     O_RDWR | O_CREAT, 0, 0, 0, 0);
    if (fd < 0) exit(1);

    // write() advances the file offset.
    ret = syscall(SYS_write, fd, (unsigned long long)"hello", 5, 0, 0, 0);
    if (ret != 5) exit(2);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 5) exit(3);

    // read() reads from the current file offset.
    if (syscall(SYS_lseek, fd, 0, SEEK_SET, 0, 0, 0) != 0) exit(4);
    ret = syscall(SYS_read, fd, (unsigned long long)buf, 5, 0, 0, 0);
    if (ret != 5) exit(5);
    if (strcmp(buf, "hello") != 0) exit(6);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 5) exit(7);

    // write() writes at the current file offset.
    ret = syscall(SYS_write, fd, (unsigned long long)"world", 5, 0, 0, 0);
    if (ret != 5) exit(8);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(9);

    // Read the whole file.
    if (syscall(SYS_lseek, fd, 0, SEEK_SET, 0, 0, 0) != 0) exit(10);
    for (int i = 0; i < 16; ++i) buf[i] = 0;
    ret = syscall(SYS_read, fd, (unsigned long long)buf, 10, 0, 0, 0);
    if (ret != 10) exit(11);
    if (strcmp(buf, "helloworld") != 0) exit(12);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(13);

    // read() at EOF returns 0 and keeps the offset unchanged.
    ret = syscall(SYS_read, fd, (unsigned long long)buf, 5, 0, 0, 0);
    if (ret != 0) exit(14);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(15);

    // Invalid fd.
    ret = (int)syscall(SYS_read, -1, (unsigned long long)buf, 1, 0, 0, 0);
    if (ret != -EBADF) exit(16);
    ret = (int)syscall(SYS_write, -1, (unsigned long long)"x", 1, 0, 0, 0);
    if (ret != -EBADF) exit(17);

    // write() on a read-only fd.
    int ro = syscall(SYS_open, (unsigned long long)"read_write", O_RDONLY, 0,
                     0, 0, 0);
    if (ro < 0) exit(18);
    ret = (int)syscall(SYS_write, ro, (unsigned long long)"x", 1, 0, 0, 0);
    if (ret != -EBADF) exit(19);
    for (int i = 0; i < 16; ++i) buf[i] = 0;
    ret = syscall(SYS_read, ro, (unsigned long long)buf, 1, 0, 0, 0);
    if (ret != 1) exit(20);
    if (buf[0] != 'h') exit(21);
    if (syscall(SYS_lseek, ro, 0, SEEK_CUR, 0, 0, 0) != 1) exit(22);

    // read() on a write-only fd.
    int wo = syscall(SYS_open, (unsigned long long)"read_write", O_WRONLY, 0,
                     0, 0, 0);
    if (wo < 0) exit(23);
    ret = (int)syscall(SYS_read, wo, (unsigned long long)buf, 1, 0, 0, 0);
    if (ret != -EBADF) exit(24);
    ret = syscall(SYS_write, wo, (unsigned long long)"x", 1, 0, 0, 0);
    if (ret != 1) exit(25);
    if (syscall(SYS_lseek, wo, 0, SEEK_CUR, 0, 0, 0) != 1) exit(26);

    exit(0);
}
