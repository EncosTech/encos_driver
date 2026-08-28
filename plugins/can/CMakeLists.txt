cmake_minimum_required(VERSION 3.18)
project(EncosCanPlugin)

set(CMAKE_CXX_STANDARD 17)

if(NOT TARGET FdBrokerClient AND NOT ENCOS_STATIC_MODE)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../utils/fdBroker
        ${CMAKE_BINARY_DIR}/plugins/utils/fdBroker)
endif()

add_library(CanPlatformSupport STATIC
    can_socket_setup.cc
)
target_link_libraries(CanPlatformSupport PUBLIC
    EncosMotorDriverBase
)
target_include_directories(CanPlatformSupport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
set_target_properties(CanPlatformSupport PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(CanHandle STATIC
    can_handle.cc
)
target_include_directories(CanHandle PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)
set_target_properties(CanHandle PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(CanPluginCore STATIC
    can_adapter.cc
)
target_link_libraries(CanPluginCore PUBLIC
    CanHandle
    CanPlatformSupport
    EncosMotorDriverBase
)
if(NOT ENCOS_STATIC_MODE)
    target_link_libraries(CanPluginCore PUBLIC FdBrokerClient)
endif()

if(ENCOS_BUILD_TESTS AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    encos_register_plugin_tests(
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/test/can_socket_setup_test.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/test/can_virtual_interface_test.cc
        LIBRARIES
            CanHandle
            CanPlatformSupport
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
target_include_directories(CanPluginCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_include_directories(CanPluginCore PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)
set_target_properties(CanPluginCore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT ENCOS_STATIC_MODE)
    add_executable(CanFdBrokerExecutable
        can_fd_broker_executable.cc
    )
    target_link_libraries(CanFdBrokerExecutable PRIVATE
        CanPlatformSupport
        EncosMotorDriverBase
    )
    target_include_directories(CanFdBrokerExecutable PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils/fdBroker
    )
    target_include_directories(CanFdBrokerExecutable PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/..
    )
    set_target_properties(CanFdBrokerExecutable PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
    )

    add_library(CanPlugin SHARED
        plugin_entry.cc
    )
    encos_configure_plugin_library(CanPlugin)
    target_link_libraries(CanPlugin PRIVATE CanPluginCore)
    target_include_directories(CanPlugin PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
    )
    add_dependencies(CanPlugin CanFdBrokerExecutable)

    if(ENCOS_ENABLE_INSTALL)
        install(TARGETS CanFdBrokerExecutable
            RUNTIME DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR}
        )

        set(PLUGIN_EXECUTABLE_BASENAME "CanFdBrokerExecutable")
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/set_socket_capabilities.sh.in
            ${CMAKE_CURRENT_BINARY_DIR}/set_socketcan_capabilities.sh
            @ONLY
        )
        install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/set_socketcan_capabilities.sh
            DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR}
        )
        install(CODE "
            if(\"\$ENV{DESTDIR}\" STREQUAL \"\")
                execute_process(
                    COMMAND \"\${CMAKE_INSTALL_PREFIX}/${ENCOS_PLUGIN_INSTALL_LIBDIR}/set_socketcan_capabilities.sh\" \"\${CMAKE_INSTALL_PREFIX}\"
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
                    message(FATAL_ERROR \"Failed to set capabilities for CanFdBrokerExecutable (exit \${_setcap_result}).\")
                endif()
            endif()
        ")
    endif()
endif()
