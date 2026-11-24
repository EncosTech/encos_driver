# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.18)
project(FdBrokerClient)

set(CMAKE_CXX_STANDARD 17)

add_library(FdBrokerClient STATIC
    fd_broker_client.cc
)
target_link_libraries(FdBrokerClient PUBLIC
    EncosMotorDriverBase
)
target_include_directories(FdBrokerClient PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(FdBrokerClient PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# 文件能力触发安全加载模式，broker 不得依赖可重定位目录中的项目共享库。
if(ENCOS_PORTABLE_LINUX)
    add_library(FdBrokerStandaloneLog STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/platform/log.cc
    )
    target_include_directories(FdBrokerStandaloneLog PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../../include
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src
    )
    target_compile_definitions(FdBrokerStandaloneLog PUBLIC
        ENCOS_ENABLE_SPDLOG=$<BOOL:${ENCOS_ENABLE_SPDLOG}>
    )
    if(ENCOS_ENABLE_SPDLOG)
        target_sources(FdBrokerStandaloneLog PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/platform/log_spdlog.cc
        )
        target_link_libraries(FdBrokerStandaloneLog PRIVATE spdlog::spdlog fmt::fmt)
    else()
        target_sources(FdBrokerStandaloneLog PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/../../../../src/platform/log_stdout.cc
        )
    endif()
    target_link_libraries(FdBrokerStandaloneLog PRIVATE pthread)
endif()

if(ENCOS_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/fd_broker_privilege_test.cc
        LIBRARIES FdBrokerClient
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
