cmake_minimum_required(VERSION 3.18)
project(EthercatBaseHandle)

set(CMAKE_CXX_STANDARD 17)

add_library(EthercatBaseHandle STATIC
    ethercat_base_handle.cc
)
target_link_libraries(EthercatBaseHandle PUBLIC
    EncosMotorDriverBase
    soem
)
target_include_directories(EthercatBaseHandle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../external/SOEM/include
    ${CMAKE_BINARY_DIR}/external/SOEM/include
)
if(ENCOS_ENABLE_TRACY)
    target_include_directories(EthercatBaseHandle PUBLIC
        ${ENCOS_TRACY_SOURCE_DIR}/public
    )
    target_compile_definitions(EthercatBaseHandle PUBLIC
        TRACY_ENABLE
        TRACY_ON_DEMAND
    )
endif()
set_target_properties(EthercatBaseHandle PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

if(ENCOS_BUILD_TESTS)
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/ethercat_base_handle_test.cc
        LIBRARIES EthercatBaseHandle
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
