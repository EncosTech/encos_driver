cmake_minimum_required(VERSION 3.18)
project(EncosEthercatIGHPlugin)

set(CMAKE_CXX_STANDARD 17)

if(NOT TARGET EthercatBaseHandle)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../utils/ethercatBase
        ${CMAKE_BINARY_DIR}/plugins/utils/ethercatBase)
endif()

find_package(IGHEtherCAT REQUIRED)

add_library(EthercatIGHHandle STATIC
    ethercat_igh_handle.cc
)
target_link_libraries(EthercatIGHHandle PUBLIC
    EthercatBaseHandle
    IGHEtherCAT::IGHEtherCAT
)
target_include_directories(EthercatIGHHandle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
set_target_properties(EthercatIGHHandle PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(EthercatIGHPluginCore STATIC
    ethercat_igh_adapter.cc
)
target_link_libraries(EthercatIGHPluginCore PUBLIC
    EthercatIGHHandle
    EncosMotorDriverBase
)
target_include_directories(EthercatIGHPluginCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(EthercatIGHPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
set_target_properties(EthercatIGHPluginCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ENCOS_STATIC_MODE)
    add_library(EthercatIGHPlugin SHARED
        plugin_entry.cc
    )
    encos_configure_plugin_library(EthercatIGHPlugin)
    target_link_libraries(EthercatIGHPlugin PRIVATE EthercatIGHPluginCore)
    target_include_directories(EthercatIGHPlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
