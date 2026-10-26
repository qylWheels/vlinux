#include "utils.h"

#define SYS_open 2
#define SYS_lseek 8
#define SYS_pread64 17
#define SYS_pwrite64 18
#define SYS_exit 60

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100

#define SEEK_SET 0
#define SEEK_CUR 1

#define EBADF 9
#define EINVAL 22

int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

void exit(int status) { syscall(SYS_exit, status, 0, 0, 0, 0, 0); }

void _start(void) {
    char buf[32] = {0};
    long long ret;

    int fd = syscall(SYS_open, (unsigned long long)"pread_pwrite",
                     O_RDWR | O_CREAT, 0, 0, 0, 0);
    if (fd < 0) exit(1);

    // Write the initial content.
    ret = syscall(SYS_pwrite64, fd, (unsigned long long)"hello", 5, 0, 0, 0);
    if (ret != 5) exit(2);
    ret = syscall(SYS_pwrite64, fd, (unsigned long long)"world", 5, 5, 0, 0);
    if (ret != 5) exit(3);

    // pwrite() does not change the file offset.
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 0) exit(4);

    // pread() reads the whole file without changing the offset.
    ret = syscall(SYS_pread64, fd, (unsigned long long)buf, 10, 0, 0, 0);
    if (ret != 10) exit(5);
    if (strcmp(buf, "helloworld") != 0) exit(6);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 0) exit(7);

    // Move the current offset, then overwrite a range with pwrite().
    if (syscall(SYS_lseek, fd, 3, SEEK_SET, 0, 0, 0) != 3) exit(8);
    ret = syscall(SYS_pwrite64, fd, (unsigned long long)"WORLD", 5, 5, 0, 0);
    if (ret != 5) exit(9);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 3) exit(10);

    // Verify the overwritten content.
    for (int i = 0; i < 32; ++i) buf[i] = 0;
    ret = syscall(SYS_pread64, fd, (unsigned long long)buf, 10, 0, 0, 0);
    if (ret != 10) exit(11);
    if (strcmp(buf, "helloWORLD") != 0) exit(12);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 3) exit(13);

    // Read at a non-zero offset.
    for (int i = 0; i < 32; ++i) buf[i] = 0;
    ret = syscall(SYS_pread64, fd, (unsigned long long)buf, 4, 3, 0, 0);
    if (ret != 4) exit(14);
    if (strcmp(buf, "loWO") != 0) exit(15);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 3) exit(16);

    // pread() at EOF returns 0 and keeps the offset unchanged.
    ret = syscall(SYS_pread64, fd, (unsigned long long)buf, 5, 10, 0, 0);
    if (ret != 0) exit(17);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 3) exit(18);

    // pwrite() beyond EOF extends the file with a zero-filled gap.
    ret = syscall(SYS_pwrite64, fd, (unsigned long long)"!", 1, 12, 0, 0);
    if (ret != 1) exit(19);
    for (int i = 0; i < 32; ++i) buf[i] = 0;
    ret = syscall(SYS_pread64, fd, (unsigned long long)buf, 13, 0, 0, 0);
    if (ret != 13) exit(20);
    if (strcmp(buf, "helloWORLD") != 0) exit(21);
    if (buf[10] != 0) exit(22);
    if (buf[11] != 0) exit(23);
    if (buf[12] != '!') exit(24);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 3) exit(25);

    // Negative offsets are invalid.
    ret = (int)syscall(SYS_pread64, fd, (unsigned long long)buf, 1,
                       (unsigned long long)-1, 0, 0);
    if (ret != -EINVAL) exit(26);
    ret = (int)syscall(SYS_pwrite64, fd, (unsigned long long)"x", 1,
                       (unsigned long long)-1, 0, 0);
    if (ret != -EINVAL) exit(27);

    // Invalid fd.
    ret = (int)syscall(SYS_pread64, -1, (unsigned long long)buf, 1, 0, 0, 0);
    if (ret != -EBADF) exit(28);
    ret = (int)syscall(SYS_pwrite64, -1, (unsigned long long)"x", 1, 0, 0, 0);
    if (ret != -EBADF) exit(29);

    // pwrite() on a read-only fd.
    int ro = syscall(SYS_open, (unsigned long long)"pread_pwrite", O_RDONLY, 0,
                     0, 0, 0);
    if (ro < 0) exit(30);
    ret = (int)syscall(SYS_pwrite64, ro, (unsigned long long)"x", 1, 0, 0, 0);
    if (ret != -EBADF) exit(31);
    for (int i = 0; i < 32; ++i) buf[i] = 0;
    ret = syscall(SYS_pread64, ro, (unsigned long long)buf, 1, 0, 0, 0);
    if (ret != 1) exit(32);
    if (buf[0] != 'h') exit(33);

    // pread() on a write-only fd.
    int wo = syscall(SYS_open, (unsigned long long)"pread_pwrite", O_WRONLY, 0,
                     0, 0, 0);
    if (wo < 0) exit(34);
    ret = (int)syscall(SYS_pread64, wo, (unsigned long long)buf, 1, 0, 0, 0);
    if (ret != -EBADF) exit(35);
    ret = syscall(SYS_pwrite64, wo, (unsigned long long)"x", 1, 0, 0, 0);
    if (ret != 1) exit(36);

    exit(0);
}
