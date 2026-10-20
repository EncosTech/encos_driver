# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

# 安装消费模式样例工程：不使用驱动源码，仅凭 find_package 构建设备插件。
# 顶层 CMakeLists.txt（文件 (1)）：解析驱动包，并对每个插件目录注册一次。
cmake_minimum_required(VERSION 3.18)
project(installed_device_plugin_project)
set(CMAKE_CXX_STANDARD 17)

find_package(encos_driver REQUIRED)

add_emd_plugin(hello "${CMAKE_CURRENT_SOURCE_DIR}/plugins/device/hello" DEVICE)
