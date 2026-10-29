#include "utils.h"

#define SYS_open 2
#define SYS_close 3
#define SYS_faccessat 269
#define SYS_exit 60

#define O_RDONLY 00
#define O_RDWR 02
#define O_CREAT 0100

#define AT_FDCWD -100

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define ENOENT 2
#define EBADF 9
#define EFAULT 14
#define ENOTDIR 20
#define EINVAL 22

void exit(int status) { syscall(SYS_exit, status, 0, 0, 0, 0, 0); }

void _start(void) {
    long long ret;

    int fd = syscall(SYS_open, (unsigned long long)"faccessat_file",
                     O_RDWR | O_CREAT, 0, 0, 0, 0);
    if (fd < 0) exit(1);

    // Every valid mode returns 0 for an existing file, because the
    // permission subsystem is not implemented yet.
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"faccessat_file", F_OK, 0, 0, 0);
    if (ret != 0) exit(2);
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"faccessat_file", R_OK, 0, 0, 0);
    if (ret != 0) exit(3);
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"faccessat_file", W_OK, 0, 0, 0);
    if (ret != 0) exit(4);
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"faccessat_file", X_OK, 0, 0, 0);
    if (ret != 0) exit(5);
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"faccessat_file", R_OK | W_OK | X_OK, 0,
                  0, 0);
    if (ret != 0) exit(6);

    // Absolute pathname of an existing file.
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"/faccessat_file", F_OK, 0, 0, 0);
    if (ret != 0) exit(7);

    // The root directory exists.
    ret = syscall(SYS_faccessat, AT_FDCWD, (unsigned long long)"/", F_OK, 0,
                  0, 0);
    if (ret != 0) exit(8);
    ret = syscall(SYS_faccessat, AT_FDCWD, (unsigned long long)"/",
                  R_OK | W_OK | X_OK, 0, 0, 0);
    if (ret != 0) exit(9);

    // Relative pathname referred by a valid directory fd.
    int root_fd = syscall(SYS_open, (unsigned long long)"/", O_RDONLY, 0, 0, 0,
                          0);
    if (root_fd < 0) exit(10);
    ret = syscall(SYS_faccessat, root_fd,
                  (unsigned long long)"faccessat_file", F_OK, 0, 0, 0);
    if (ret != 0) exit(11);
    ret = syscall(SYS_faccessat, root_fd,
                  (unsigned long long)"faccessat_file", R_OK | W_OK | X_OK, 0,
                  0, 0);
    if (ret != 0) exit(12);

    // Nonexistent files.
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"no_such_file", F_OK, 0, 0, 0);
    if (ret != -ENOENT) exit(13);
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"/no_such_file", F_OK, 0, 0, 0);
    if (ret != -ENOENT) exit(14);

    // Invalid mode bits.
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"faccessat_file", 8, 0, 0, 0);
    if (ret != -EINVAL) exit(15);
    ret = syscall(SYS_faccessat, AT_FDCWD,
                  (unsigned long long)"faccessat_file",
                  R_OK | W_OK | X_OK | 8, 0, 0, 0);
    if (ret != -EINVAL) exit(16);

    // Invalid dirfd.
    ret = syscall(SYS_faccessat, -1, (unsigned long long)"faccessat_file",
                  F_OK, 0, 0, 0);
    if (ret != -EBADF) exit(17);
    ret = syscall(SYS_faccessat, -2, (unsigned long long)"faccessat_file",
                  F_OK, 0, 0, 0);
    if (ret != -EBADF) exit(18);

    // A regular file is not a directory.
    ret = syscall(SYS_faccessat, fd, (unsigned long long)"faccessat_file",
                  F_OK, 0, 0, 0);
    if (ret != -ENOTDIR) exit(19);

    // Unmapped pathname pointer.
    ret = syscall(SYS_faccessat, AT_FDCWD, 0, F_OK, 0, 0, 0);
    if (ret != -EFAULT) exit(20);

    // Closed dirfd.
    if (syscall(SYS_close, root_fd, 0, 0, 0, 0, 0) != 0) exit(21);
    ret = syscall(SYS_faccessat, root_fd,
                  (unsigned long long)"faccessat_file", F_OK, 0, 0, 0);
    if (ret != -EBADF) exit(22);

    exit(0);
}
