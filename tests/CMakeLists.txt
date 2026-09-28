# Subdirectories
add_subdirectory(syscall_tests)

# Catch2
Include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.15.0
)
FetchContent_MakeAvailable(Catch2)
add_executable(tests main.cpp)
target_link_libraries(tests PRIVATE Catch2::Catch2WithMain cpptrace::cpptrace)
target_link_libraries(tests PRIVATE vlinux)
target_include_directories(tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
