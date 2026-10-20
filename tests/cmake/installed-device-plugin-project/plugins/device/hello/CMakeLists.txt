# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

# 插件目标（文件 (3)）：目标名必须与 plugin.cmake 的 ENCOS_PLUGIN_DYNAMIC_TARGET 一致。
cmake_minimum_required(VERSION 3.18)
project(hello)

add_library(HelloPlugin SHARED
    hello.cc
    plugin_entry.cc
)
encos_configure_plugin_library(HelloPlugin)
target_link_libraries(HelloPlugin PRIVATE Encos::encos_driver)
target_include_directories(HelloPlugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
