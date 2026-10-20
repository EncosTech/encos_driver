# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

# 运行期探针工程：仅链接已安装的驱动，验证插件发现与设备创建。
cmake_minimum_required(VERSION 3.18)
project(installed_device_plugin_probe)
set(CMAKE_CXX_STANDARD 17)

find_package(encos_driver REQUIRED)

add_executable(probe main.cc)
target_link_libraries(probe PRIVATE Encos::encos_driver)
