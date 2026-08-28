cmake_minimum_required(VERSION 3.18)
project(EncosSlcanPlugin)

set(CMAKE_CXX_STANDARD 17)

if(NOT TARGET serialib)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../external/serialib
        ${CMAKE_BINARY_DIR}/external/serialib)
    set_target_properties(serialib PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(SlcanPluginCore STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/slcan_adapter.cc
)
target_link_libraries(SlcanPluginCore PUBLIC
    serialib
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
