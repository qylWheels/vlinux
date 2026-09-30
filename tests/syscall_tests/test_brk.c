unsigned long long syscall(int syscall_no, unsigned long long arg1,
                           unsigned long long arg2, unsigned long long arg3,
                           unsigned long long arg4, unsigned long long arg5,
                           unsigned long long arg6) {
    unsigned long long ret;
    register unsigned long long rdi_val __asm__("rdi") = arg1;
    register unsigned long long rsi_val __asm__("rsi") = arg2;
    register unsigned long long rdx_val __asm__("rdx") = arg3;
    register unsigned long long r10_val __asm__("r10") = arg4;
    register unsigned long long r8_val __asm__("r8") = arg5;
    register unsigned long long r9_val __asm__("r9") = arg6;
    asm volatile("syscall"
                 : "=rax"(ret)
                 : "rax"(syscall_no), "r"(rdi_val), "r"(rsi_val), "r"(rdx_val),
                   "r"(r10_val), "r"(r8_val), "r"(r9_val)
                 : "rcx", "r11", "memory");
    return ret;
}

void _start(void) {
    const int sys_brk = 12;
    const int sys_exit = 60;

    // Get current break.
    void *brk = (void *)syscall(sys_brk, 0, 0, 0, 0, 0, 0);

    // Set break.
    syscall(sys_brk, (unsigned long long)brk + 4096 * 2 + 1, 0, 0, 0, 0, 0);

    for (;;);

    return;
}
