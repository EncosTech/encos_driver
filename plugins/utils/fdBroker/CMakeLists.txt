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

if(ENCOS_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/fd_broker_privilege_test.cc
        LIBRARIES FdBrokerClient
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
