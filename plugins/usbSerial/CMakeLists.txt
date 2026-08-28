cmake_minimum_required(VERSION 3.18)
project(EncosUsbSerialDirectPlugin)

set(CMAKE_CXX_STANDARD 17)

if(NOT TARGET serialib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../external/serialib
        ${CMAKE_BINARY_DIR}/external/serialib)
    set_target_properties(serialib PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(UsbSerialPluginCore STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_serial_adapter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/usb_serial_sender.cc
)
target_link_libraries(UsbSerialPluginCore PUBLIC
    serialib
    EncosMotorDriverBase
)
target_include_directories(UsbSerialPluginCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(UsbSerialPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
set_target_properties(UsbSerialPluginCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ENCOS_STATIC_MODE)
    add_library(UsbSerialPlugin SHARED
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_entry.cc
    )
    encos_configure_plugin_library(UsbSerialPlugin)
    target_link_libraries(UsbSerialPlugin PRIVATE UsbSerialPluginCore)
    target_include_directories(UsbSerialPlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

if(ENCOS_BUILD_TESTS)
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/usb_serial_sender_test.cc
        LIBRARIES UsbSerialPluginCore
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
