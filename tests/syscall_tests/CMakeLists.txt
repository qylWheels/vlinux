add_executable(test_brk test_brk.c)
target_compile_options(
    test_brk PRIVATE
    -ffreestanding -fno-sanitize=all
)
target_link_options(test_brk PRIVATE -nostdlib)
