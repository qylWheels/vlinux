static inline unsigned long long syscall(
    int syscall_no, unsigned long long arg1, unsigned long long arg2,
    unsigned long long arg3, unsigned long long arg4, unsigned long long arg5,
    unsigned long long arg6) {
    unsigned long long ret;
    register unsigned long long r10_val __asm__("r10") = arg4;
    register unsigned long long r8_val __asm__("r8") = arg5;
    register unsigned long long r9_val __asm__("r9") = arg6;
    asm volatile("syscall"
                 : "=a"(ret)
                 : "a"(syscall_no), "D"(arg1), "S"(arg2), "d"(arg3),
                   "r"(r10_val), "r"(r8_val), "r"(r9_val)
                 : "rcx", "r11", "memory");
    return ret;
}
