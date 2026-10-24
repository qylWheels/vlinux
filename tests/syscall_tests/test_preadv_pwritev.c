#include "utils.h"

#define SYS_open 2
#define SYS_exit 60
#define SYS_preadv 295
#define SYS_pwritev 296

#define O_RDONLY 00
#define O_WRONLY 01
#define O_RDWR 02
#define O_CREAT 0100

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
    // Open a file.
    int fd = syscall(SYS_open, (unsigned long long)"hello", O_RDONLY | O_CREAT,
                     0, 0, 0, 0);
    if (fd < 0) {
        exit(1);
    }

    // Scattered write.
    struct iovec write_iov[] = {
        {(void *)"hello", 5},
        {(void *)"world", 5},
    };
    int write_bytes = syscall(SYS_pwritev, fd, (unsigned long long)write_iov, 2,
                              0x1000, 0, 0);
    if (write_bytes < 10) {
        exit(2);
    }

    // Scattered read.
    char read_buf[16] = {0};
    struct iovec read_iov[] = {
        {(void *)read_buf, 5},
        {(void *)read_buf + 5, 5},
    };
    int read_bytes =
        syscall(SYS_preadv, fd, (unsigned long long)read_iov, 2, 0x1000, 0, 0);
    if (read_bytes < 10) {
        exit(3);
    }

    // Compare strings.
    if (strcmp(read_buf, "hello world") != 0) {
        exit(4);
    }

    exit(0);
}
