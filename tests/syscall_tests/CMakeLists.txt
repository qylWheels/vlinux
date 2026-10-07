function(add_syscall_test test_name)
    add_executable(${test_name} ${test_name}.c)
    target_compile_options(
        ${test_name} PRIVATE
        -ffreestanding -fno-sanitize=all
    )
    target_link_options(${test_name} PRIVATE -nostdlib -static)
endfunction()

add_syscall_test(test_mmap_munmap)
add_syscall_test(test_mprotect)
add_syscall_test(test_brk)
add_syscall_test(test_clone)
add_syscall_test(test_fork)
add_syscall_test(test_exit)
