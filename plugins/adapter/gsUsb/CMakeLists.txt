# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.18)
project(EncosGsUsbPlugin)

set(CMAKE_CXX_STANDARD 17)

add_library(GsUsbPluginCore STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/gs_usb_adapter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol/gs_usb_protocol.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/winusb_device.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/transport/winusb_enumeration.cc
)
target_link_libraries(GsUsbPluginCore PUBLIC EncosMotorDriverBase)
target_link_libraries(GsUsbPluginCore PRIVATE winusb setupapi)
target_include_directories(GsUsbPluginCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    ${CMAKE_CURRENT_SOURCE_DIR}/transport
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_include_directories(GsUsbPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
)
set_target_properties(GsUsbPluginCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ENCOS_STATIC_MODE)
    add_library(GsUsbPlugin SHARED
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_entry.cc
    )
    encos_configure_plugin_library(GsUsbPlugin)
    target_link_libraries(GsUsbPlugin PRIVATE GsUsbPluginCore)
    target_include_directories(GsUsbPlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

if(ENCOS_BUILD_TESTS)
    encos_register_plugin_tests(
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/test/gs_usb_protocol_test.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/test/gs_usb_timing_test.cc
        LIBRARIES GsUsbPluginCore
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/protocol
    )
endif()
