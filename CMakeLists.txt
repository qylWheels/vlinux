cmake_minimum_required(VERSION 3.20)

project(vlinux)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

include(FetchContent)

# Unicorn.
FetchContent_Declare(
    unicorn
    GIT_REPOSITORY https://github.com/unicorn-engine/unicorn.git
    GIT_TAG        v2.1.4
)
FetchContent_MakeAvailable(unicorn)

# ELFIO.
FetchContent_Declare(
    elfio
    GIT_REPOSITORY https://github.com/serge1/ELFIO.git
    GIT_TAG        Release_3.12
)
FetchContent_MakeAvailable(elfio)

set(SRC_FILES src/mm.cpp src/elf_loader.cpp)

add_library(vlinux SHARED ${SRC_FILES})
target_include_directories(vlinux PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(vlinux PRIVATE unicorn elfio)
