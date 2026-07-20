cmake_minimum_required(VERSION 3.20)

project(vlinux)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

set(SRC_FILES src/mm.cpp src/vm.cpp)

add_library(vlinux SHARED ${SRC_FILES})
target_include_directories(vlinux PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_include_directories(vlinux PRIVATE ${CMAKE_SOURCE_DIR}/third_party)
