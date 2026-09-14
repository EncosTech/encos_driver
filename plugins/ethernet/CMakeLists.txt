cmake_minimum_required(VERSION 3.18)
project(EncosEthernetPlugin)
if(MSVC)
    add_compile_options(/utf-8)
endif()
add_library(EthernetProtocol STATIC protocol/gateway_payload.cc protocol/ipv4.cc protocol/management_protocol.cc)
set_target_properties(EthernetProtocol PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(EthernetProtocol PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(EthernetProtocol PUBLIC EncosMotorDriverBase)
add_library(EthernetManagement STATIC ethernet_management.cc)
set_target_properties(EthernetManagement PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(EthernetManagement PUBLIC EthernetProtocol)
add_library(EthernetAdapterSupport STATIC ethernet_adapter.cc)
set_target_properties(EthernetAdapterSupport PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(EthernetAdapterSupport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_link_libraries(EthernetAdapterSupport PUBLIC EthernetManagement)
if(WIN32)
    target_sources(EthernetManagement PRIVATE transport/windows/datagram.cc)
    target_compile_definitions(EthernetManagement PUBLIC _WIN32_WINNT=0x0600 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(EthernetManagement PUBLIC ws2_32 iphlpapi)
    target_sources(EthernetAdapterSupport PRIVATE transport/windows/transport.cc transport/windows/interface.cc)
    if(ENCOS_STATIC_MODE)
        target_sources(EthernetAdapterSupport PRIVATE transport/windows/network_configuration.cc)
    else()
        target_link_libraries(EthernetAdapterSupport PRIVATE shell32)
        add_executable(EthernetNetworkHelperExecutable transport/windows/network_helper.cc
            transport/windows/network_configuration.cc)
        target_link_libraries(EthernetNetworkHelperExecutable PRIVATE iphlpapi)
        set(ethernet_helper EthernetNetworkHelperExecutable)
    endif()
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(XDP REQUIRED IMPORTED_TARGET libxdp)
    pkg_check_modules(BPF REQUIRED IMPORTED_TARGET libbpf)
    find_program(BPF_CLANG NAMES clang clang-20 clang-19 clang-18 clang-17 clang-16 REQUIRED)
    set(bpf_output "${CMAKE_BINARY_DIR}/plugins/ethernet_xdp.bpf.o")
    add_custom_command(OUTPUT "${bpf_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/plugins"
        COMMAND ${BPF_CLANG} -O2 -g -target bpf
            -I/usr/include/${CMAKE_LIBRARY_ARCHITECTURE} -I${CMAKE_CURRENT_SOURCE_DIR}/..
            -c ${CMAKE_CURRENT_SOURCE_DIR}/transport/linux/xdp.bpf.c -o ${bpf_output}
        DEPENDS transport/linux/xdp.bpf.c transport/linux/xdp_shared.h VERBATIM)
    add_custom_target(EthernetXdpFilter DEPENDS "${bpf_output}")
    target_sources(EthernetManagement PRIVATE transport/linux/datagram.cc)
    target_sources(EthernetAdapterSupport PRIVATE transport/linux/xdp.cc transport/linux/factory.cc)
    find_library(ETHERNET_CAP_LIBRARY cap REQUIRED)
    target_link_libraries(EthernetAdapterSupport PRIVATE PkgConfig::XDP ${ETHERNET_CAP_LIBRARY})
    add_library(EthernetXdpSession STATIC transport/linux/xdp_session.cc)
    set_target_properties(EthernetXdpSession PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_include_directories(EthernetXdpSession PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
    target_link_libraries(EthernetXdpSession PUBLIC PkgConfig::XDP PkgConfig::BPF
        ${ETHERNET_CAP_LIBRARY})
    if(ENCOS_STATIC_MODE)
        set(bpf_header "${CMAKE_CURRENT_BINARY_DIR}/ethernet_xdp_embedded.generated.h")
        add_custom_command(OUTPUT "${bpf_header}"
            COMMAND ${CMAKE_COMMAND} "-DINPUT=${bpf_output}" "-DOUTPUT=${bpf_header}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/embed_bpf.cmake"
            DEPENDS "${bpf_output}" embed_bpf.cmake VERBATIM)
        target_sources(EthernetAdapterSupport PRIVATE "${bpf_header}")
        target_include_directories(EthernetAdapterSupport PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(EthernetAdapterSupport PRIVATE EthernetXdpSession)
    else()
        add_executable(EthernetXdpBrokerExecutable transport/linux/xdp_broker.cc)
        target_link_libraries(EthernetXdpBrokerExecutable PRIVATE EthernetXdpSession)
        add_dependencies(EthernetXdpBrokerExecutable EthernetXdpFilter)
        set(ethernet_helper EthernetXdpBrokerExecutable)
    endif()
endif()
if(ethernet_helper)
    set_target_properties(${ethernet_helper} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins")
endif()
if(NOT ENCOS_STATIC_MODE)
    add_library(EthernetPlugin SHARED plugin_entry.cc)
    encos_configure_plugin_library(EthernetPlugin)
    target_link_libraries(EthernetPlugin PRIVATE EthernetAdapterSupport)
    add_dependencies(EthernetPlugin ${ethernet_helper})
endif()
if(ENCOS_ENABLE_INSTALL)
    install(TARGETS ${ethernet_helper} RUNTIME DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR})
    if(NOT WIN32)
        install(FILES ${bpf_output} DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR})
        set(PLUGIN_CAPABILITIES "cap_net_raw,cap_net_admin,cap_bpf+ep")
        set(PLUGIN_EXECUTABLE_BASENAME "EthernetXdpBrokerExecutable")
        configure_file(
            ${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/set_socket_capabilities.sh.in
            ${CMAKE_CURRENT_BINARY_DIR}/set_ethernet_capabilities.sh
            @ONLY
        )
        install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/set_ethernet_capabilities.sh
            DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR}
        )
        install(CODE "
            if(\"\$ENV{DESTDIR}\" STREQUAL \"\")
                execute_process(
                    COMMAND \"\${CMAKE_INSTALL_PREFIX}/${ENCOS_PLUGIN_INSTALL_LIBDIR}/set_ethernet_capabilities.sh\" \"\${CMAKE_INSTALL_PREFIX}\"
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
                    message(FATAL_ERROR \"Failed to set capabilities for EthernetXdpBrokerExecutable (exit \${_setcap_result}).\")
                endif()
            endif()
        ")
    endif()
endif()
option(ENCOS_ETHERNET_BUILD_TESTS "Build Ethernet protocol and platform tests independently" OFF)
set(ethernet_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/test/ethernet_protocol_test.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/test/ethernet_initialization_test.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/test/ethernet_discovery_test.cc)
if(WIN32)
    list(APPEND ethernet_tests ${CMAKE_CURRENT_SOURCE_DIR}/test/ethernet_windows_test.cc)
else()
    list(APPEND ethernet_tests ${CMAKE_CURRENT_SOURCE_DIR}/test/ethernet_datagram_linux_test.cc)
endif()
if(ENCOS_BUILD_TESTS)
    set(ethernet_test_libraries EthernetAdapterSupport)
    if(NOT WIN32)
        list(APPEND ethernet_test_libraries EthernetXdpSession)
    endif()
    encos_register_plugin_tests(SOURCES ${ethernet_tests}
        LIBRARIES ${ethernet_test_libraries}
        INCLUDE_DIRECTORIES ${CMAKE_CURRENT_SOURCE_DIR}/.. ${CMAKE_CURRENT_BINARY_DIR})
    if(ENCOS_STATIC_MODE AND NOT WIN32)
        encos_register_plugin_tests(
            SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/ethernet_static_test.cc
            LIBRARIES EthernetAdapterSupport EthernetXdpSession
            INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endif()
if(ENCOS_ETHERNET_BUILD_TESTS)
    find_package(GTest CONFIG REQUIRED)
    add_executable(EthernetTests ${ethernet_tests}
        ${CMAKE_CURRENT_SOURCE_DIR}/../utils/emr1/test/relay_frame_test.cc)
    target_link_libraries(EthernetTests PRIVATE EthernetAdapterSupport GTest::gtest_main)
    if(NOT WIN32)
        target_link_libraries(EthernetTests PRIVATE EthernetXdpSession)
    endif()
    if(WIN32)
        target_sources(EthernetTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../tests/core/thread_priority_test.cc)
    endif()
    set_target_properties(EthernetTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME EthernetTests COMMAND EthernetTests)
    set_tests_properties(EthernetTests PROPERTIES TIMEOUT 30)
endif()
if(WIN32 AND NOT ENCOS_STATIC_MODE AND (ENCOS_BUILD_TESTS OR ENCOS_ETHERNET_BUILD_TESTS))
    add_executable(EthernetWindowsPluginSmoke test/ethernet_windows_plugin_smoke.cc)
    target_link_libraries(EthernetWindowsPluginSmoke PRIVATE EncosMotorDriver)
    set_target_properties(EthernetWindowsPluginSmoke PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
    add_test(NAME EthernetWindowsPluginSmoke COMMAND EthernetWindowsPluginSmoke)
    set_tests_properties(EthernetWindowsPluginSmoke PROPERTIES TIMEOUT 15
        ENVIRONMENT "ENCOS_PLUGIN_PATH=$<TARGET_FILE_DIR:EthernetPlugin>")
endif()
