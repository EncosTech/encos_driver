# Encos 原生运行期测试。
#
# 本文件在插件发现前被 include，以便已启用插件通过
# encos_register_plugin_tests() 声明其测试。所有目标创建和 CTest 发现均在
# encos_configure_tests() 中统一完成，且只能在全部插件加载后调用。

function(encos_register_plugin_tests)
    if(NOT ENCOS_BUILD_TESTS)
        return()
    endif()

    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES;INCLUDE_DIRECTORIES;DEPENDENCIES" ${ARGN})
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "encos_register_plugin_tests requires at least one source file")
    endif()

    set_property(GLOBAL APPEND PROPERTY _ENCOS_PLUGIN_TEST_SOURCES ${ARG_SOURCES})
    set_property(GLOBAL APPEND PROPERTY _ENCOS_PLUGIN_TEST_LIBRARIES ${ARG_LIBRARIES})
    set_property(GLOBAL APPEND PROPERTY _ENCOS_PLUGIN_TEST_INCLUDE_DIRECTORIES
        ${ARG_INCLUDE_DIRECTORIES})
    set_property(GLOBAL APPEND PROPERTY _ENCOS_PLUGIN_TEST_DEPENDENCIES ${ARG_DEPENDENCIES})
endfunction()

function(encos_configure_tests)
    if(NOT ENCOS_BUILD_TESTS)
        return()
    endif()

    find_package(GTest REQUIRED)
    include(CheckCXXSourceCompiles)

    set(CMAKE_REQUIRED_INCLUDES "${CMAKE_CURRENT_SOURCE_DIR}/include")
    unset(ENCOS_PORT_VALID_CAPACITY_COMPILES CACHE)
    check_cxx_source_compiles([=[
#include "utils/port.h"
        int main() { encos::Port<3> port; return port.Pop().has_value(); }
    ]=] ENCOS_PORT_VALID_CAPACITY_COMPILES)
    if(NOT ENCOS_PORT_VALID_CAPACITY_COMPILES)
        message(FATAL_ERROR "Port<3> must compile")
    endif()

    function(encos_expect_port_compile_failure check_name source)
        unset(${check_name} CACHE)
        check_cxx_source_compiles("${source}" ${check_name})
        if(${check_name})
            message(FATAL_ERROR "${check_name} unexpectedly compiled")
        endif()
    endfunction()

    encos_expect_port_compile_failure(ENCOS_PORT_CAPACITY_ZERO_COMPILES [=[
#include "utils/port.h"
        int main() { encos::Port<0> port; }
    ]=])
    encos_expect_port_compile_failure(ENCOS_PORT_CAPACITY_ONE_COMPILES [=[
#include "utils/port.h"
        int main() { encos::Port<1> port; }
    ]=])
    encos_expect_port_compile_failure(ENCOS_PORT_CAPACITY_TWO_COMPILES [=[
#include "utils/port.h"
        int main() { encos::Port<2> port; }
    ]=])
    encos_expect_port_compile_failure(ENCOS_PORT_NONTRIVIAL_MESSAGE_COMPILES [=[
#include "utils/port.h"
        struct NonTrivialMessage { ~NonTrivialMessage() {} };
        int main() { encos::Port<3, NonTrivialMessage> port; }
    ]=])

    set(_encos_saved_try_compile_target_type "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
    unset(ENCOS_DRIVER_API_FUNCTIONS_COMPILE CACHE)
    check_cxx_source_compiles([=[
#include <type_traits>
#include "encos/encos_driver.h"

#ifdef MakeAdapter
#error "MakeAdapter must be a real function, not a macro"
#endif
#ifdef UnloadAdapterByInterfaceName
#error "UnloadAdapterByInterfaceName must be a real function, not a macro"
#endif
#ifdef DeleteAdapter
#error "DeleteAdapter must be a real function, not a macro"
#endif
#ifdef DeleteBus
#error "DeleteBus must be a real function, not a macro"
#endif
#ifdef DeleteMotor
#error "DeleteMotor must be a real function, not a macro"
#endif
#ifdef DeleteBattery
#error "DeleteBattery must be a real function, not a macro"
#endif
#ifdef DeleteImu
#error "DeleteImu must be a real function, not a macro"
#endif
#ifdef DeletePms
#error "DeletePms must be a real function, not a macro"
#endif
#ifdef DeleteGlove
#error "DeleteGlove must be a real function, not a macro"
#endif

        int main() {
            using MakeAdapterFunction = encos::BaseAdapter* (*)(
                const std::string&, const std::string&, const std::string&, encos::LogLevel);
            static_assert(std::is_same_v<decltype(&encos::MakeAdapter), MakeAdapterFunction>);
            static_assert(std::is_same_v<decltype(&encos::DeleteAdapter),
                                         bool (*)(encos::BaseAdapter*)>);
            static_assert(std::is_same_v<decltype(&encos::DeleteBus), bool (*)(encos::Bus*)>);
            static_assert(std::is_same_v<decltype(&encos::DeleteMotor), bool (*)(encos::Motor*)>);
            static_assert(std::is_same_v<decltype(&encos::DeleteBattery),
                                         bool (*)(encos::Battery*)>);
            static_assert(std::is_same_v<decltype(&encos::DeleteImu), bool (*)(encos::Imu*)>);
            static_assert(std::is_same_v<decltype(&encos::DeletePms), bool (*)(encos::Pms*)>);
            static_assert(std::is_same_v<decltype(&encos::DeleteGlove),
                                         bool (*)(encos::Glove*)>);
            return 0;
        }
    ]=] ENCOS_DRIVER_API_FUNCTIONS_COMPILE)
    if(NOT ENCOS_DRIVER_API_FUNCTIONS_COMPILE)
        message(FATAL_ERROR "Driver compatibility API must use addressable functions")
    endif()
    set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_encos_saved_try_compile_target_type}")
    unset(_encos_saved_try_compile_target_type)

    include(GoogleTest)

    add_executable(${ENCOS_DRIVER_BASE_TEST_TARGET}
        src/wasm/wasm_runtime.cc
        tests/core/test_adapter.cc
        tests/core/adapter_routing_test.cc
        tests/core/device/battery_callback_test.cc
        tests/core/bus_port_scan_test.cc
        tests/core/driver_manager_test.cc
        tests/core/plugin/plugin_ownership_test.cc
        tests/core/port_test.cc
        tests/core/platform_sync_test.cc
        tests/core/thread_priority_test.cc
        tests/core/operation_gate_test.cc
        tests/core/log_writer_test.cc
        tests/core/motor/pvt_control_test.cc
        tests/core/motor/pos_control_test.cc
        tests/core/motor/spd_control_test.cc
        tests/core/motor/cur_control_test.cc
        tests/core/motor/tor_control_test.cc
        tests/core/motor/stop_test.cc
        tests/core/motor/brake_test.cc
        tests/core/motor/pvt_range_test.cc
        tests/core/motor/can_id_test.cc
        tests/core/motor/control_parameters_test.cc
        tests/core/motor/status_lifecycle_test.cc
        tests/core/motor/logging_test.cc
        tests/core/motor/waiter_test_access.cc
        tests/core/motor/response_waiter_test.cc
        tests/core/device/imu_callback_test.cc
        tests/core/device/pms_callback_test.cc
        tests/core/runtime/wasm_runtime_test.cc
        tests/glove_callback_test.cc
    )
    target_include_directories(${ENCOS_DRIVER_BASE_TEST_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/core
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/core/support
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/core/device
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/core/motor
    )
    if(TARGET ThreadPriorityHelper)
        target_compile_definitions(${ENCOS_DRIVER_BASE_TEST_TARGET} PRIVATE
            ENCOS_TEST_THREAD_PRIORITY_HELPER="$<TARGET_FILE:ThreadPriorityHelper>"
        )
    endif()
    target_link_libraries(${ENCOS_DRIVER_BASE_TEST_TARGET} PRIVATE
        ${ENCOS_DRIVER_TARGET}
        FakeAdapterSupport
        GTest::gtest_main
        ${ENCOS_ZSTD_TARGET}
    )

    if(TARGET EthercatBaseHandle)
        target_sources(${ENCOS_DRIVER_BASE_TEST_TARGET} PRIVATE
            tests/ethercat_base_handle_test.cc
        )
        target_link_libraries(${ENCOS_DRIVER_BASE_TEST_TARGET} PRIVATE EthercatBaseHandle)
    endif()

    add_executable(EncosManagerShutdownThrowTest
        tests/core/manager_shutdown_throw_test.cc
    )
    target_include_directories(EncosManagerShutdownThrowTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/core/support
    )
    target_link_libraries(EncosManagerShutdownThrowTest PRIVATE
        ${ENCOS_DRIVER_BASE_TARGET}
    )
    add_test(NAME EncosManagerShutdownContainsCleanupExceptions
             COMMAND EncosManagerShutdownThrowTest)

    add_executable(EncosMotorLogShutdownTest
        tests/core/motor_log_shutdown_test.cc
    )
    target_include_directories(EncosMotorLogShutdownTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(EncosMotorLogShutdownTest PRIVATE
        ${ENCOS_DRIVER_BASE_TARGET}
        ${ENCOS_ZSTD_TARGET}
    )
    add_test(NAME EncosMotorLogShutdownFlushes
             COMMAND EncosMotorLogShutdownTest)
    set_tests_properties(EncosMotorLogShutdownFlushes PROPERTIES TIMEOUT 3)

    get_property(_plugin_test_sources GLOBAL PROPERTY _ENCOS_PLUGIN_TEST_SOURCES)
    get_property(_plugin_test_libraries GLOBAL PROPERTY _ENCOS_PLUGIN_TEST_LIBRARIES)
    get_property(_plugin_test_include_directories GLOBAL
        PROPERTY _ENCOS_PLUGIN_TEST_INCLUDE_DIRECTORIES)
    get_property(_plugin_test_dependencies GLOBAL PROPERTY _ENCOS_PLUGIN_TEST_DEPENDENCIES)

    if(ENCOS_STATIC_MODE)
        list(APPEND _plugin_test_sources tests/core/plugin/plugin_static_test.cc)
    else()
        list(APPEND _plugin_test_sources tests/core/plugin/plugin_test.cc)

        add_library(PluginCacheTestPlugin SHARED
            tests/core/plugin/plugin_loader_test_plugin.cc
        )
        set_target_properties(PluginCacheTestPlugin PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
        )
        target_include_directories(PluginCacheTestPlugin PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/plugins
        )
        target_link_libraries(PluginCacheTestPlugin PRIVATE ${ENCOS_DRIVER_BASE_TARGET})
        list(APPEND _plugin_test_dependencies PluginCacheTestPlugin)
    endif()

    list(REMOVE_DUPLICATES _plugin_test_sources)
    list(REMOVE_DUPLICATES _plugin_test_libraries)
    list(REMOVE_DUPLICATES _plugin_test_include_directories)
    list(REMOVE_DUPLICATES _plugin_test_dependencies)

    add_executable(${ENCOS_DRIVER_PLUGINS_TEST_TARGET}
        ${_plugin_test_sources}
    )
    target_include_directories(${ENCOS_DRIVER_PLUGINS_TEST_TARGET} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/core
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/core/support
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/core/device
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/core/motor
        ${_plugin_test_include_directories}
    )
    target_link_libraries(${ENCOS_DRIVER_PLUGINS_TEST_TARGET} PRIVATE
        ${ENCOS_DRIVER_TARGET}
        GTest::gtest_main
        GTest::gmock_main
        ${_plugin_test_libraries}
    )
    if(_plugin_test_dependencies)
        add_dependencies(${ENCOS_DRIVER_PLUGINS_TEST_TARGET} ${_plugin_test_dependencies})
    endif()

    if(ENCOS_ENABLE_TSAN)
        gtest_discover_tests(${ENCOS_DRIVER_BASE_TEST_TARGET}
            DISCOVERY_MODE PRE_TEST
            PROPERTIES ENVIRONMENT "ENCOS_DISABLE_PRIORITY_GUI=1"
        )
    else()
        gtest_discover_tests(${ENCOS_DRIVER_BASE_TEST_TARGET}
            PROPERTIES ENVIRONMENT "ENCOS_DISABLE_PRIORITY_GUI=1"
        )
    endif()
    gtest_discover_tests(${ENCOS_DRIVER_PLUGINS_TEST_TARGET}
        PROPERTIES ENVIRONMENT "ENCOS_DISABLE_PRIORITY_GUI=1"
    )

    if(ENCOS_ENABLE_TSAN)
        add_test(
            NAME EncosPortThreadSanitizer
            COMMAND ${ENCOS_DRIVER_BASE_TEST_TARGET}
                    --gtest_filter=PortProgressTests.*:PortConcurrencyTests.*:PortLivenessRegressionTests.*:OperationRegistryTest.*:PVTRangeTests.ConcurrentRangeReadsObserveCoherentSnapshots:FakeAdapterTests.ConcurrentCommandsHistoryReadsAndObserverReentryAreSafe:AdapterRoutingTest.SerializesRegisteredCallbackDelivery:AdapterSoftSyncTests.SameBatterySerializesConcurrentCommandPublication:AdapterSoftSyncTests.SamePmsSerializesConcurrentCommandPublication:ResponseWaiterTests.*:DriverManagerTest.*Deletion*
        )
        set_tests_properties(EncosPortThreadSanitizer PROPERTIES
            LABELS "port;fake;routing;waiter;deletion;tsan"
        )
    endif()

    find_program(ENCOS_EMXX_BIN NAMES em++)
    if(ENCOS_EMXX_BIN)
        add_test(
            NAME EncosPortEmscriptenCompile
            COMMAND ${ENCOS_EMXX_BIN}
                    -std=c++17
                    -I${CMAKE_CURRENT_SOURCE_DIR}/include
                    -c ${CMAKE_CURRENT_SOURCE_DIR}/tests/core/port_emscripten_compile.cc
                    -o ${CMAKE_CURRENT_BINARY_DIR}/port_emscripten_compile.o
        )
        set_tests_properties(EncosPortEmscriptenCompile PROPERTIES LABELS "port;emscripten")
    endif()
endfunction()
