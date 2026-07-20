cmake_minimum_required(VERSION 3.20)

project(vlinux)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SRC_FILES src/mm.cpp)

add_library(vlinux SHARED ${SRC_FILES})
