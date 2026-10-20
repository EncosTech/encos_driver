# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.18)
project(EncosTestClaimDevicePlugin)

set(CMAKE_CXX_STANDARD 17)

add_library(TestClaimDeviceCore STATIC
    test_claim_device.cc
)
target_link_libraries(TestClaimDeviceCore PUBLIC
    EncosMotorDriverBase
)
target_include_directories(TestClaimDeviceCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(TestClaimDeviceCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../..
)
set_target_properties(TestClaimDeviceCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ENCOS_STATIC_MODE)
    add_library(TestClaimDevicePlugin SHARED
        plugin_entry.cc
    )
    encos_configure_plugin_library(TestClaimDevicePlugin)
    target_link_libraries(TestClaimDevicePlugin PRIVATE TestClaimDeviceCore)
    target_include_directories(TestClaimDevicePlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

# 测试依赖 FakeAdapterSupport；Fake 插件是可选项，未启用时跳过测试注册。
if(ENCOS_BUILD_TESTS AND TARGET FakeAdapterSupport)
    set(_test_claim_device_dependencies "")
    if(NOT ENCOS_STATIC_MODE)
        list(APPEND _test_claim_device_dependencies TestClaimDevicePlugin)
    endif()
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/test_claim_device_test.cc
        LIBRARIES TestClaimDeviceCore FakeAdapterSupport
        DEPENDENCIES ${_test_claim_device_dependencies}
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
