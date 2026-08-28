cmake_minimum_required(VERSION 3.18)
project(EncosEthercatWindowsPlugin)

set(CMAKE_CXX_STANDARD 17)

if(NOT TARGET EthercatBaseHandle)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../utils/ethercatBase
        ${CMAKE_BINARY_DIR}/plugins/utils/ethercatBase)
endif()

if(NOT TARGET soem)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../external/SOEM
        ${CMAKE_BINARY_DIR}/external/SOEM)
    set_target_properties(soem PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_library(EthercatWindowsHandle STATIC
    ethercat_windows_handle.cc
)
target_link_libraries(EthercatWindowsHandle PUBLIC
    EthercatBaseHandle
    soem
)
target_include_directories(EthercatWindowsHandle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../external/SOEM/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
set_target_properties(EthercatWindowsHandle PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(EthercatWindowsPluginCore STATIC
    ethercat_windows_adapter.cc
)
target_link_libraries(EthercatWindowsPluginCore PUBLIC
    EthercatWindowsHandle
    EncosMotorDriverBase
)
target_include_directories(EthercatWindowsPluginCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(EthercatWindowsPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
set_target_properties(EthercatWindowsPluginCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ENCOS_STATIC_MODE)
    add_library(EthercatWindowsPlugin SHARED
        plugin_entry.cc
    )
    encos_configure_plugin_library(EthercatWindowsPlugin)
    target_link_libraries(EthercatWindowsPlugin PRIVATE EthercatWindowsPluginCore)
    target_include_directories(EthercatWindowsPlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

if(ENCOS_BUILD_TESTS)
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/ethercat_windows_discovery_test.cc
        LIBRARIES FakeAdapterSupport
    )
endif()
