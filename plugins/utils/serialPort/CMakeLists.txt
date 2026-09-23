# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

add_library(EncosSerialPort STATIC)
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_sources(EncosSerialPort PRIVATE serial_port.emscripten.cc)
    target_link_options(EncosSerialPort INTERFACE
        "SHELL:--pre-js ${CMAKE_CURRENT_SOURCE_DIR}/web_serial.js")
    set_property(TARGET EncosSerialPort APPEND PROPERTY INTERFACE_LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/web_serial.js)
elseif(WIN32)
    target_sources(EncosSerialPort PRIVATE serial_port.windows.cc)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(EncosSerialPort PRIVATE serial_port.linux.cc)
else()
    message(FATAL_ERROR "Native serial ports require Linux or Windows")
endif()
if(NOT EMSCRIPTEN AND NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_sources(EncosSerialPort PRIVATE serial_port.native.cc)
    target_include_directories(EncosSerialPort PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../../src)
endif()
target_link_libraries(EncosSerialPort PUBLIC EncosMotorDriverBase)
target_include_directories(EncosSerialPort PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(EncosSerialPort PUBLIC cxx_std_17)
set_target_properties(EncosSerialPort PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(ENCOS_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(ENCOS_WEB_SERIAL_NODE node)
    if(ENCOS_WEB_SERIAL_NODE)
        add_test(NAME EncosWebSerialBridge
            COMMAND ${ENCOS_WEB_SERIAL_NODE} --test
                ${CMAKE_CURRENT_SOURCE_DIR}/test/web_serial_bridge_test.mjs)
    endif()
endif()
