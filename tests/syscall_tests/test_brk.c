unsigned long long syscall(int syscall_no, unsigned long long arg1,
                           unsigned long long arg2, unsigned long long arg3,
                           unsigned long long arg4, unsigned long long arg5,
                           unsigned long long arg6) {
    unsigned long long ret;
    register long r10_val __asm__("r10") = arg4;
    register long r8_val __asm__("r8") = arg5;
    register long r9_val __asm__("r9") = arg6;
    asm volatile("syscall"
                 : "=rax"(ret)
                 : "rax"(syscall_no), "rdi"(arg1), "rsi"(arg2), "rdx"(arg3),
                   "r"(r10_val), "r"(r8_val), "r"(r9_val)
                 : "rcx", "r11", "memory");
    return ret;
}

void _start(void) {
    const int sys_brk = 12;
    const int sys_exit = 60;

    // Get current break.
    void *ret = (void *)syscall(sys_brk, 0, 0, 0, 0, 0, 0);

    for (;;);

    return;
}
