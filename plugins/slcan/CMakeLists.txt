cmake_minimum_required(VERSION 3.18)
project(EncosSlcanPlugin)

set(CMAKE_CXX_STANDARD 17)

if(NOT TARGET EncosSerialPort)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../utils/serialPort
        ${CMAKE_BINARY_DIR}/plugins/utils/serialPort)
endif()

add_library(SlcanPluginCore STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/slcan_adapter.cc
)
target_link_libraries(SlcanPluginCore PUBLIC
    EncosSerialPort
    EncosMotorDriverBase
)
target_include_directories(SlcanPluginCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(SlcanPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
set_target_properties(SlcanPluginCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ENCOS_STATIC_MODE)
    add_library(SlcanPlugin SHARED
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_entry.cc
    )
    encos_configure_plugin_library(SlcanPlugin)
    target_link_libraries(SlcanPlugin PRIVATE SlcanPluginCore)
    target_include_directories(SlcanPlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

if(ENCOS_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/slcan_adapter_test.cc
        LIBRARIES SlcanPluginCore
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../../src
    )
endif()
