cmake_minimum_required(VERSION 3.10)
project(serialib)

add_library(serialib OBJECT lib/serialib.cpp)
target_include_directories(serialib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/lib)
