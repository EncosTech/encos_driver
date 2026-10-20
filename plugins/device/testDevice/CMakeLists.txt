# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.18)
project(EncosTestDevicePlugin)

set(CMAKE_CXX_STANDARD 17)

add_library(TestDeviceDeviceCore STATIC
    test_device.cc
)
target_link_libraries(TestDeviceDeviceCore PUBLIC
    EncosMotorDriverBase
)
target_include_directories(TestDeviceDeviceCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(TestDeviceDeviceCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
set_target_properties(TestDeviceDeviceCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ENCOS_STATIC_MODE)
    add_library(TestDevicePlugin SHARED
        plugin_entry.cc
    )
    encos_configure_plugin_library(TestDevicePlugin)
    target_link_libraries(TestDevicePlugin PRIVATE TestDeviceDeviceCore)
    target_include_directories(TestDevicePlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# 测试依赖 FakeAdapterSupport；Fake 插件是可选项，未启用时跳过测试注册，
# 以免把两个独立可选项耦合。
if(ENCOS_BUILD_TESTS AND TARGET FakeAdapterSupport)
    set(_test_device_dependencies "")
    if(NOT ENCOS_STATIC_MODE)
        list(APPEND _test_device_dependencies TestDevicePlugin)
    endif()
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/test_device_test.cc
        LIBRARIES TestDeviceDeviceCore FakeAdapterSupport
        DEPENDENCIES ${_test_device_dependencies}
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
