cmake_minimum_required(VERSION 3.18)
project(EncosEthercatPlugin)

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

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT TARGET FdBrokerClient AND NOT ENCOS_STATIC_MODE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../utils/fdBroker
        ${CMAKE_BINARY_DIR}/plugins/utils/fdBroker)
endif()

add_library(EthercatHandle STATIC
    ethercat_handle.cc
)
target_link_libraries(EthercatHandle PUBLIC
    EthercatBaseHandle
    soem
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ENCOS_STATIC_MODE)
    target_link_libraries(EthercatHandle PUBLIC FdBrokerClient)
endif()
target_include_directories(EthercatHandle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../external/SOEM/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ENCOS_STATIC_MODE)
    target_include_directories(EthercatHandle PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils/fdBroker
    )
endif()
set_target_properties(EthercatHandle PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(WIN32)
    target_sources(EthercatHandle PRIVATE transport/windows/port.cc)
    target_link_libraries(EthercatHandle PUBLIC iphlpapi)
else()
    target_sources(EthercatHandle PRIVATE transport/linux/port.cc)
endif()


if(ENCOS_BUILD_TESTS)
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/ethercat_recovery_test.cc
                ${CMAKE_CURRENT_SOURCE_DIR}/test/ethercat_discovery_test.cc
        LIBRARIES EthercatHandle FakeAdapterSupport
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()

add_library(EthercatPluginCore STATIC
    ethercat_adapter.cc
)
target_link_libraries(EthercatPluginCore PUBLIC
    EthercatHandle
    EncosMotorDriverBase
)
target_include_directories(EthercatPluginCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(EthercatPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
set_target_properties(EthercatPluginCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ENCOS_STATIC_MODE)
    add_executable(EthercatFdBrokerExecutable
        transport/linux/fd_broker_executable.cc
    )
    target_link_libraries(EthercatFdBrokerExecutable PRIVATE
        EncosMotorDriverBase
    )
    target_include_directories(EthercatFdBrokerExecutable PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils/fdBroker
    )
    target_include_directories(EthercatFdBrokerExecutable PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    set_target_properties(EthercatFdBrokerExecutable PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )

    if(ENCOS_ENABLE_INSTALL)
        install(TARGETS EthercatFdBrokerExecutable
            RUNTIME DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR}
        )

        set(PLUGIN_EXECUTABLE_BASENAME "EthercatFdBrokerExecutable")
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/set_socket_capabilities.sh.in
            ${CMAKE_CURRENT_BINARY_DIR}/set_ethercat_capabilities.sh
            @ONLY
        )
        install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/set_ethercat_capabilities.sh
            DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR}
        )
        install(CODE "
            if(\"\$ENV{DESTDIR}\" STREQUAL \"\")
                execute_process(
                    COMMAND \"\${CMAKE_INSTALL_PREFIX}/${ENCOS_PLUGIN_INSTALL_LIBDIR}/set_ethercat_capabilities.sh\" \"\${CMAKE_INSTALL_PREFIX}\"
                    RESULT_VARIABLE _setcap_result
                    OUTPUT_VARIABLE _setcap_output
                    ERROR_VARIABLE _setcap_error
                )
                if(_setcap_output)
                    message(STATUS \"\${_setcap_output}\")
                endif()
                if(_setcap_error)
                    message(STATUS \"\${_setcap_error}\")
                endif()
                if(NOT _setcap_result EQUAL 0)
                    message(FATAL_ERROR \"Failed to set capabilities for EthercatFdBrokerExecutable (exit \${_setcap_result}).\")
                endif()
            endif()
        ")
    endif()
endif()

if(NOT ENCOS_STATIC_MODE)
    add_library(EthercatPlugin SHARED
        plugin_entry.cc
    )
    encos_configure_plugin_library(EthercatPlugin)
    target_link_libraries(EthercatPlugin PRIVATE EthercatPluginCore)
    target_include_directories(EthercatPlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(TARGET EthercatFdBrokerExecutable)
        add_dependencies(EthercatPlugin EthercatFdBrokerExecutable)
    endif()
endif()

option(ENCOS_ETHERCAT_BUILD_TESTS "Build standalone SOEM recovery tests" OFF)
if(ENCOS_ETHERCAT_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_executable(EthercatTests
        test/ethercat_recovery_test.cc
        ../utils/ethercatBase/test/ethercat_base_handle_test.cc)
    target_include_directories(EthercatTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../..)
    target_link_libraries(EthercatTests PRIVATE EthercatHandle GTest::gtest_main)
    set_target_properties(EthercatTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME EthercatTests COMMAND EthercatTests)
    set_tests_properties(EthercatTests PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}" TIMEOUT 30)
endif()

if(WIN32 AND ENCOS_ETHERCAT_BUILD_TESTS AND NOT ENCOS_STATIC_MODE)
    add_executable(EthercatPluginSmoke test/ethercat_plugin_smoke.cc)
    target_link_libraries(EthercatPluginSmoke PRIVATE EncosMotorDriver)
    set_target_properties(EthercatPluginSmoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME EthercatPluginSmoke COMMAND EthercatPluginSmoke)
    set_tests_properties(EthercatPluginSmoke PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        ENVIRONMENT "ENCOS_PLUGIN_PATH=$<TARGET_FILE_DIR:EthercatPlugin>"
        TIMEOUT 15)
endif()
