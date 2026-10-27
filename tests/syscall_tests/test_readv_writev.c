#include "utils.h"

#define SYS_open 2
#define SYS_lseek 8
#define SYS_readv 19
#define SYS_writev 20
#define SYS_exit 60

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100

#define SEEK_SET 0
#define SEEK_CUR 1

#define EBADF 9
#define EINVAL 22

struct iovec {
    void *iov_base;
    unsigned long long iov_len;
};

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

    int fd = syscall(SYS_open, (unsigned long long)"readv_writev",
                     O_RDWR | O_CREAT, 0, 0, 0, 0);
    if (fd < 0) exit(1);

    // writev() writes all iovecs and advances the file offset.
    struct iovec wv[] = {
        {(void *)"hello", 5},
        {(void *)"world", 5},
    };
    ret = syscall(SYS_writev, fd, (unsigned long long)wv, 2, 0, 0, 0);
    if (ret != 10) exit(2);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(3);

    // readv() reads all iovecs and advances the file offset.
    if (syscall(SYS_lseek, fd, 0, SEEK_SET, 0, 0, 0) != 0) exit(4);
    struct iovec rv[] = {
        {(void *)buf, 3},
        {(void *)(buf + 3), 7},
    };
    ret = syscall(SYS_readv, fd, (unsigned long long)rv, 2, 0, 0, 0);
    if (ret != 10) exit(5);
    if (strcmp(buf, "helloworld") != 0) exit(6);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(7);

    // Empty iovec arrays transfer nothing and keep the offset unchanged.
    ret = syscall(SYS_writev, fd, (unsigned long long)wv, 0, 0, 0, 0);
    if (ret != 0) exit(8);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(9);
    ret = syscall(SYS_readv, fd, (unsigned long long)rv, 0, 0, 0, 0);
    if (ret != 0) exit(10);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(11);

    // readv() at EOF returns 0 and keeps the offset unchanged.
    for (int i = 0; i < 16; ++i) buf[i] = 0;
    ret = syscall(SYS_readv, fd, (unsigned long long)rv, 2, 0, 0, 0);
    if (ret != 0) exit(12);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(13);

    // A short read fills iovecs until EOF.
    if (syscall(SYS_lseek, fd, 8, SEEK_SET, 0, 0, 0) != 8) exit(14);
    struct iovec rv2[] = {
        {(void *)buf, 2},
        {(void *)(buf + 2), 5},
    };
    for (int i = 0; i < 16; ++i) buf[i] = 0;
    ret = syscall(SYS_readv, fd, (unsigned long long)rv2, 2, 0, 0, 0);
    if (ret != 2) exit(15);
    if (strcmp(buf, "ld") != 0) exit(16);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(17);

    // writev() writes at the current file offset.
    if (syscall(SYS_lseek, fd, 5, SEEK_SET, 0, 0, 0) != 5) exit(18);
    struct iovec wv2[] = {
        {(void *)"WORLD", 5},
    };
    ret = syscall(SYS_writev, fd, (unsigned long long)wv2, 1, 0, 0, 0);
    if (ret != 5) exit(19);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(20);

    // Verify the overwritten content.
    if (syscall(SYS_lseek, fd, 0, SEEK_SET, 0, 0, 0) != 0) exit(21);
    struct iovec rv3[] = {
        {(void *)buf, 10},
    };
    for (int i = 0; i < 16; ++i) buf[i] = 0;
    ret = syscall(SYS_readv, fd, (unsigned long long)rv3, 1, 0, 0, 0);
    if (ret != 10) exit(22);
    if (strcmp(buf, "helloWORLD") != 0) exit(23);
    if (syscall(SYS_lseek, fd, 0, SEEK_CUR, 0, 0, 0) != 10) exit(24);

    // Invalid fd.
    ret = (int)syscall(SYS_readv, -1, (unsigned long long)rv3, 1, 0, 0, 0);
    if (ret != -EBADF) exit(25);
    ret = (int)syscall(SYS_writev, -1, (unsigned long long)wv2, 1, 0, 0, 0);
    if (ret != -EBADF) exit(26);

    // Negative iovcnt is invalid.
    ret = (int)syscall(SYS_readv, fd, (unsigned long long)rv3, -1, 0, 0, 0);
    if (ret != -EINVAL) exit(27);
    ret = (int)syscall(SYS_writev, fd, (unsigned long long)wv2, -1, 0, 0, 0);
    if (ret != -EINVAL) exit(28);

    // writev() on a read-only fd.
    int ro = syscall(SYS_open, (unsigned long long)"readv_writev", O_RDONLY, 0,
                     0, 0, 0);
    if (ro < 0) exit(29);
    ret = (int)syscall(SYS_writev, ro, (unsigned long long)wv2, 1, 0, 0, 0);
    if (ret != -EBADF) exit(30);
    struct iovec one[] = {
        {(void *)buf, 1},
    };
    for (int i = 0; i < 16; ++i) buf[i] = 0;
    ret = syscall(SYS_readv, ro, (unsigned long long)one, 1, 0, 0, 0);
    if (ret != 1) exit(31);
    if (buf[0] != 'h') exit(32);

    // readv() on a write-only fd.
    int wo = syscall(SYS_open, (unsigned long long)"readv_writev", O_WRONLY, 0,
                     0, 0, 0);
    if (wo < 0) exit(33);
    ret = (int)syscall(SYS_readv, wo, (unsigned long long)one, 1, 0, 0, 0);
    if (ret != -EBADF) exit(34);
    ret = syscall(SYS_writev, wo, (unsigned long long)wv2, 1, 0, 0, 0);
    if (ret != 5) exit(35);

    exit(0);
}
