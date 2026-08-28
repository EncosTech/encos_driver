cmake_minimum_required(VERSION 3.18)
project(encos_driver VERSION 3.2.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
set(ENCOS_PLUGIN_INSTALL_LIBDIR "${CMAKE_INSTALL_LIBDIR}/encosPlugins")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(EncosOptions)
include(EncosPlugins)
include(EncosGeneratedFiles)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(ENCOS_BUILD_TESTS "Build EncosMotorDriver tests" OFF)
option(ENCOS_ENABLE_RELAY "Build WebSocket relay support" ON)
option(ENCOS_ENABLE_TSAN "Run Port concurrency tests with ThreadSanitizer" OFF)
option(ENCOS_ENABLE_TRACY "Enable Tracy performance profiling" OFF)

# 插件在发现期间声明其测试，测试目标在全部插件注册后统一创建。
include(EncosTests)

set(ENCOS_TRACY_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/tracy")
if(ENCOS_ENABLE_TRACY)
    include(FetchContent)
    FetchContent_Declare(
        encos_tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG 05cceee0df3b8d7c6fa87e9638af311dbabc63cb
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        SOURCE_DIR "${ENCOS_TRACY_SOURCE_DIR}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/external/tracy"
    )
    FetchContent_GetProperties(encos_tracy)
    if(NOT encos_tracy_POPULATED)
        if(EXISTS "${ENCOS_TRACY_SOURCE_DIR}/public/TracyClient.cpp")
            message(STATUS "Reusing Tracy profiler source: ${ENCOS_TRACY_SOURCE_DIR}")
        else()
            message(STATUS "Fetching Tracy profiler into ${ENCOS_TRACY_SOURCE_DIR}")
        endif()
        FetchContent_Populate(encos_tracy)
    endif()
    set(ENCOS_TRACY_SOURCE_DIR "${encos_tracy_SOURCE_DIR}")
    if(NOT EXISTS "${ENCOS_TRACY_SOURCE_DIR}/public/TracyClient.cpp")
        message(FATAL_ERROR "Tracy profiler source is incomplete: ${ENCOS_TRACY_SOURCE_DIR}")
    endif()
    message(STATUS "Tracy profiler ready: ${ENCOS_TRACY_SOURCE_DIR}")
    message(STATUS "Tracy profiling mode: on-demand (events start after collector connects)")
endif()

if(ENCOS_ENABLE_TSAN)
    if(EMSCRIPTEN OR WIN32 OR NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "ENCOS_ENABLE_TSAN requires native GCC or Clang")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()

set(ENCOS_DRIVER_TARGET EncosMotorDriver)
set(ENCOS_DRIVER_BASE_TARGET EncosMotorDriverBase)
set(ENCOS_DRIVER_BASE_TEST_TARGET EncosMotorDriverBaseTests)
set(ENCOS_DRIVER_PLUGINS_TEST_TARGET EncosMotorDriverPluginsTests)

encos_configure_options()

if(ENCOS_STATIC_MODE)
    add_compile_definitions(ENCOS_STATIC_MODE)
endif()

find_program(CLANG_FORMAT_BIN NAMES clang-format clang-format-18)
find_program(CLANG_TIDY_BIN NAMES clang-tidy clang-tidy-18)

add_custom_target(format
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/run_clang_format.sh
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Formatting project sources with clang-format"
)

add_custom_target(format-check
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/run_clang_format.sh --check
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Checking project sources with clang-format"
)

add_custom_target(tidy
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/run_clang_tidy.sh ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Running clang-tidy on project translation units"
)

if(CLANG_FORMAT_BIN STREQUAL "CLANG_FORMAT_BIN-NOTFOUND")
    set_property(TARGET format PROPERTY EXCLUDE_FROM_ALL TRUE)
    set_property(TARGET format-check PROPERTY EXCLUDE_FROM_ALL TRUE)
endif()

if(CLANG_TIDY_BIN STREQUAL "CLANG_TIDY_BIN-NOTFOUND")
    set_property(TARGET tidy PROPERTY EXCLUDE_FROM_ALL TRUE)
endif()

if(ENCOS_ENABLE_SPDLOG AND NOT TARGET spdlog)
    find_package(spdlog REQUIRED)
endif()
if(ENCOS_ENABLE_SPDLOG AND NOT TARGET fmt::fmt)
    find_package(fmt REQUIRED)
endif()
if(ENCOS_ENABLE_PYTHON)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
    find_library(ENCOS_CAP_LIBRARY NAMES cap REQUIRED)
    find_program(ENCOS_PKEXEC_EXECUTABLE NAMES pkexec PATHS /usr/bin /bin NO_DEFAULT_PATH)
    find_program(ENCOS_SETCAP_EXECUTABLE NAMES setcap PATHS /usr/sbin /sbin NO_DEFAULT_PATH)
    if(NOT ENCOS_PKEXEC_EXECUTABLE)
        set(ENCOS_PKEXEC_EXECUTABLE "")
    endif()
    if(NOT ENCOS_SETCAP_EXECUTABLE)
        set(ENCOS_SETCAP_EXECUTABLE "")
    endif()

    add_executable(ThreadPriorityHelper
        src/utils/thread_priority_helper.cc
        src/utils/thread_priority_internal.cc
    )
    target_include_directories(ThreadPriorityHelper PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    set_target_properties(ThreadPriorityHelper PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/encosPlugins"
    )
endif()

set(ENCOS_ZSTD_TARGET "")
if(NOT (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten"))
    find_package(zstd CONFIG REQUIRED)
    foreach(_zstd_candidate zstd::libzstd_shared zstd::libzstd_static zstd::libzstd)
        if(TARGET ${_zstd_candidate})
            set(ENCOS_ZSTD_TARGET ${_zstd_candidate})
            break()
        endif()
    endforeach()
    if(NOT ENCOS_ZSTD_TARGET)
        message(FATAL_ERROR "zstd was found, but it did not provide a supported CMake target")
    endif()
endif()

if(NOT ENCOS_STATIC_MODE)
    add_subdirectory(external/dylib)
    set_target_properties(dylib PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

set(MOTOR_MODEL_GENERATED_HEADER
    ${CMAKE_CURRENT_SOURCE_DIR}/include/motor/motor_model_generated.h)
set(MOTOR_MODEL_GENERATED_SOURCE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/motor/motor_model_generated.cc)
set(MOTOR_MODEL_CSV
    ${CMAKE_CURRENT_SOURCE_DIR}/src/motor/motor_models.csv)

if(ENCOS_ENABLE_PYTHON)
    add_custom_command(
        OUTPUT  ${MOTOR_MODEL_GENERATED_HEADER}
                ${MOTOR_MODEL_GENERATED_SOURCE}
        COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_motor_params.py
                --csv ${MOTOR_MODEL_CSV}
                --out-header ${MOTOR_MODEL_GENERATED_HEADER}
                --out-source ${MOTOR_MODEL_GENERATED_SOURCE}
        DEPENDS ${MOTOR_MODEL_CSV}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_motor_params.py
        COMMENT "Generating motor model parameters from CSV"
    )

    add_custom_target(motor_params_generated ALL
        DEPENDS ${MOTOR_MODEL_GENERATED_HEADER}
                ${MOTOR_MODEL_GENERATED_SOURCE}
    )
elseif(NOT ENCOS_SKIP_GENERATED_SHA1_CHECK)
    encos_verify_generated_csv_sha1(
        ${MOTOR_MODEL_CSV}
        ${MOTOR_MODEL_GENERATED_HEADER}
        ${MOTOR_MODEL_GENERATED_SOURCE}
    )
endif()

set(ENCOS_PLATFORM_OS_SOURCE src/platform/os_linux.cc)
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(ENCOS_PLATFORM_OS_SOURCE src/platform/os_emscripten.cc)
elseif(WIN32)
    set(ENCOS_PLATFORM_OS_SOURCE src/platform/os_windows.cc)
endif()

set(ENCOS_LOG_BACKEND_SOURCES src/platform/log.cc)
if(ENCOS_ENABLE_SPDLOG)
    list(APPEND ENCOS_LOG_BACKEND_SOURCES src/platform/log_spdlog.cc)
elseif(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    list(APPEND ENCOS_LOG_BACKEND_SOURCES src/platform/log_emscripten.cc)
else()
    list(APPEND ENCOS_LOG_BACKEND_SOURCES src/platform/log_stdout.cc)
endif()

set(_encos_base_library_type SHARED)
if(ENCOS_STATIC_MODE)
    set(_encos_base_library_type STATIC)
endif()

set(ENCOS_RELAY_SOURCES src/relay/relay_frame.cc)

add_library(${ENCOS_DRIVER_BASE_TARGET} ${_encos_base_library_type}
    ${ENCOS_LOG_BACKEND_SOURCES}
    ${ENCOS_PLATFORM_OS_SOURCE}
    src/adapter/base_adapter.cc
    src/driver_manager.cc
    src/driver_manager_create.cc
    src/driver_manager_destroy.cc
    src/driver_manager_status.cc
    src/driver_manager_dispatch.cc
    src/bus/bus.cc
    src/motor/motor.cc
    src/motor/motor_control.cc
    src/motor/motor_limit.cc
    src/motor/motor_log.cc
    src/motor/motor_log_transport.cc
    src/motor/motor_parameter.cc
    src/motor/math_utils.cc
    src/utils/log_writer.cc
    src/utils/thread_priority.cc
    src/utils/thread_priority_internal.cc
    src/battery/battery.cc
    src/glove/glove.cc
    src/glove/glove_calibrator.cc
    src/glove/glove_encoder.cc
    src/imu/imu.cc
    src/pms/pms.cc
    ${MOTOR_MODEL_GENERATED_SOURCE}
    ${ENCOS_RELAY_SOURCES}
)

if(ENCOS_ENABLE_TRACY)
    target_sources(${ENCOS_DRIVER_BASE_TARGET} PRIVATE
        ${ENCOS_TRACY_SOURCE_DIR}/public/TracyClient.cpp
    )
    target_include_directories(${ENCOS_DRIVER_BASE_TARGET} PRIVATE
        ${ENCOS_TRACY_SOURCE_DIR}/public
    )
    target_compile_definitions(${ENCOS_DRIVER_BASE_TARGET} PRIVATE
        TRACY_ENABLE
        TRACY_ON_DEMAND
    )
endif()

message(STATUS "EncosMotorDriverBase library type: ${_encos_base_library_type}")
target_include_directories(${ENCOS_DRIVER_BASE_TARGET}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/encos>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/external/boost_preprocessor/include>
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
set_target_properties(${ENCOS_DRIVER_BASE_TARGET} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "encosdriver_base"
    EXPORT_NAME encos_driver_base
)
if(NOT ENCOS_STATIC_MODE)
    set_target_properties(${ENCOS_DRIVER_BASE_TARGET} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    target_compile_definitions(${ENCOS_DRIVER_BASE_TARGET} PRIVATE EncosMotorDriverBase_EXPORTS)
endif()
target_compile_definitions(${ENCOS_DRIVER_BASE_TARGET} PUBLIC
    ENCOS_ENABLE_SPDLOG=$<BOOL:${ENCOS_ENABLE_SPDLOG}>
)
target_compile_definitions(${ENCOS_DRIVER_BASE_TARGET} PRIVATE
    ENCOS_PLUGIN_INSTALL_PATH="${CMAKE_INSTALL_PREFIX}/${ENCOS_PLUGIN_INSTALL_LIBDIR}"
)
if(ENCOS_ENABLE_SPDLOG)
    target_link_libraries(${ENCOS_DRIVER_BASE_TARGET} PUBLIC spdlog fmt::fmt)
endif()
if(ENCOS_ZSTD_TARGET)
    target_link_libraries(${ENCOS_DRIVER_BASE_TARGET} PRIVATE ${ENCOS_ZSTD_TARGET})
endif()
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT EMSCRIPTEN)
    target_link_libraries(${ENCOS_DRIVER_BASE_TARGET} PRIVATE ${ENCOS_CAP_LIBRARY} ${CMAKE_DL_LIBS})
    target_compile_definitions(${ENCOS_DRIVER_BASE_TARGET} PRIVATE
        ENCOS_THREAD_PRIORITY_HELPER_BUILD_PATH="${CMAKE_BINARY_DIR}/encosPlugins/ThreadPriorityHelper"
        ENCOS_THREAD_PRIORITY_HELPER_INSTALL_PATH="${CMAKE_INSTALL_PREFIX}/${ENCOS_PLUGIN_INSTALL_LIBDIR}/ThreadPriorityHelper"
        ENCOS_PKEXEC_PATH="${ENCOS_PKEXEC_EXECUTABLE}"
        ENCOS_SETCAP_PATH="${ENCOS_SETCAP_EXECUTABLE}"
    )
    add_dependencies(${ENCOS_DRIVER_BASE_TARGET} ThreadPriorityHelper)
endif()

if(ENCOS_STATIC_MODE)
    add_library(${ENCOS_DRIVER_TARGET} STATIC
        plugins/plugin_loader_static.cc
        src/driver_api.cc
    )
    message(STATUS "EncosMotorDriver library type: STATIC")
else()
    add_library(${ENCOS_DRIVER_TARGET} SHARED
        plugins/plugin_loader.cc
        src/driver_api.cc
    )
    target_link_libraries(${ENCOS_DRIVER_TARGET} PRIVATE dylib)
    set_target_properties(${ENCOS_DRIVER_TARGET} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
    )
    target_compile_definitions(${ENCOS_DRIVER_TARGET} PRIVATE EncosMotorDriver_EXPORTS)
    message(STATUS "EncosMotorDriver library type: SHARED")
endif()

target_include_directories(${ENCOS_DRIVER_TARGET}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include/encos>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/plugins
        ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(${ENCOS_DRIVER_TARGET} PUBLIC ${ENCOS_DRIVER_BASE_TARGET})
target_compile_definitions(${ENCOS_DRIVER_TARGET} PRIVATE
    ENCOS_PLUGIN_INSTALL_PATH="${CMAKE_INSTALL_PREFIX}/${ENCOS_PLUGIN_INSTALL_LIBDIR}"
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ENCOS_STATIC_MODE)
    set_target_properties(${ENCOS_DRIVER_TARGET} PROPERTIES
        INSTALL_RPATH "$ORIGIN"
    )
endif()

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    target_link_options(${ENCOS_DRIVER_BASE_TARGET} PUBLIC "-sASYNCIFY")
    target_link_options(${ENCOS_DRIVER_TARGET} PUBLIC "-sASYNCIFY")
    target_link_options(${ENCOS_DRIVER_BASE_TARGET} PUBLIC "-lwebsocket.js")
    target_link_options(${ENCOS_DRIVER_TARGET} PUBLIC "-lwebsocket.js")
    if(ENCOS_BUILD_WASM_BINDINGS)
        target_compile_options(${ENCOS_DRIVER_BASE_TARGET} PUBLIC "-fexceptions")
        target_compile_options(${ENCOS_DRIVER_TARGET} PUBLIC "-fexceptions")
        target_link_options(${ENCOS_DRIVER_BASE_TARGET} PUBLIC "-fexceptions")
        target_link_options(${ENCOS_DRIVER_TARGET} PUBLIC "-fexceptions")
    endif()
endif()

if(WIN32)
    target_link_libraries(${ENCOS_DRIVER_TARGET} PRIVATE ucrt iphlpapi)
endif()

set_target_properties(${ENCOS_DRIVER_TARGET} PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    OUTPUT_NAME "encosdriver"
    EXPORT_NAME encos_driver
)
set_target_properties(${ENCOS_DRIVER_TARGET} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(ENCOS_STATIC_MODE)
    target_compile_definitions(${ENCOS_DRIVER_TARGET} PUBLIC ENCOS_STATIC_MODE)
    target_compile_definitions(${ENCOS_DRIVER_BASE_TARGET} PUBLIC ENCOS_STATIC_MODE)
endif()

if(ENCOS_ENABLE_RELAY AND NOT (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten"))
#IXWebSocket client support(no TLS, no zlib, no install)
    set(USE_TLS OFF CACHE BOOL "" FORCE)
    set(USE_ZLIB OFF CACHE BOOL "" FORCE)
    set(IXWEBSOCKET_INSTALL OFF CACHE BOOL "" FORCE)
    set(BUILD_DEMO OFF CACHE BOOL "" FORCE)
    set(USE_WS OFF CACHE BOOL "" FORCE)
    set(USE_TEST OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/IXWebSocket)
endif()

# 发现并添加内置插件
encos_discover_builtin_plugins("${CMAKE_CURRENT_SOURCE_DIR}/plugins")

# 生成插件注册头文件
set(ENCOS_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
encos_generate_plugin_registry("${ENCOS_GENERATED_INCLUDE_DIR}")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/encos_version.h.in"
    "${ENCOS_GENERATED_INCLUDE_DIR}/encos_version.h"
    @ONLY
)
add_custom_target(version-consistency
    COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/npm/scripts/sync-version.mjs"
    COMMAND node "${CMAKE_CURRENT_SOURCE_DIR}/npm/scripts/check-version.mjs"
        "${CMAKE_CURRENT_SOURCE_DIR}/CMakeLists.txt"
        "${CMAKE_CURRENT_SOURCE_DIR}/npm/staging/package.json"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    VERBATIM
)

if(ENCOS_BUILD_WASM_BINDINGS)
    if(TARGET FakeAdapterSupport)
        target_compile_options(FakeAdapterSupport PUBLIC "-fexceptions")
        target_link_options(FakeAdapterSupport PUBLIC "-fexceptions")
    endif()
    add_executable(encosdriver_wasm
        src/wasm/wasm_runtime.cc
        src/wasm/wasm_c_api.cc
    )
    target_include_directories(encosdriver_wasm PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${ENCOS_GENERATED_INCLUDE_DIR}
    )
    target_compile_options(encosdriver_wasm PRIVATE "-fexceptions")
    target_link_libraries(encosdriver_wasm PRIVATE
        ${ENCOS_DRIVER_TARGET}
        FakeAdapterSupport
    )
    target_link_options(encosdriver_wasm PRIVATE
        "-fexceptions"
        "-sASYNCIFY"
        "-sMODULARIZE=1"
        "-sEXPORT_ES6=1"
        "-sENVIRONMENT=web,node"
        "-sALLOW_MEMORY_GROWTH=1"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_encos_get_last_error_code','_encos_get_last_error_message','_encos_clear_last_error','_encos_get_version','_encos_create_adapter','_encos_create_fake_adapter','_encos_dispose_adapter','_encos_adapter_ok','_encos_adapter_get_bus','_encos_adapter_get_bus_count','_encos_adapter_get_bus_raw_index_at','_encos_bus_get_index','_encos_bus_get_motor_with_model','_encos_bus_get_battery','_encos_bus_get_imu','_encos_motor_get_index','_encos_battery_get_status','_encos_battery_send_passive_commands','_encos_battery_clear_fault','_encos_battery_request_charging','_encos_battery_allow_discharge','_encos_imu_get_status','_encos_motor_get_status','_encos_motor_pvt_control','_encos_motor_spd_control','_encos_motor_pos_control','_encos_motor_cur_control','_encos_motor_set_pos','_encos_motor_reset_zero_pos','_encos_motor_get_float_parameter','_encos_motor_set_can_timeout','_encos_fake_seed_motor','_encos_fake_enable_auto_create_motor','_encos_fake_set_reply_mode','_encos_fake_inject_feedback','_encos_fake_set_parameter_write_policy','_encos_fake_inject_raw_message','_encos_fake_get_motor_snapshot','_encos_fake_get_command_count','_encos_fake_get_command_kind','_encos_fake_get_command_bus_index','_encos_fake_get_command_motor_index','_encos_fake_get_command_number_field','_encos_fake_get_command_int_field','_encos_fake_get_raw_message_count','_encos_fake_get_raw_message_bus_index','_encos_fake_get_raw_message_can_id','_encos_fake_get_raw_message_frame_flags','_encos_fake_get_raw_message_len','_encos_fake_get_raw_message_data_byte']"
        "-sEXPORTED_RUNTIME_METHODS=['cwrap','ccall','UTF8ToString','getValue','setValue']"
    )
    set_target_properties(encosdriver_wasm PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        OUTPUT_NAME "encosdriver_wasm"
        SUFFIX ".js"
    )
endif()

if(ENCOS_BUILD_WASM_TESTS)
    add_executable(encosdriver_wasm_logging_test
        tests/wasm/wasm_logging_unsupported_test.cc
    )
    target_include_directories(encosdriver_wasm_logging_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${ENCOS_GENERATED_INCLUDE_DIR}
    )
    target_compile_options(encosdriver_wasm_logging_test PRIVATE "-fexceptions")
    target_link_options(encosdriver_wasm_logging_test PRIVATE "-fexceptions")
    target_link_libraries(encosdriver_wasm_logging_test PRIVATE
        ${ENCOS_DRIVER_TARGET}
        FakeAdapterSupport
    )
    add_test(NAME EncosWasmLoggingUnsupported COMMAND encosdriver_wasm_logging_test)
endif()

if(ENCOS_ENABLE_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS ${ENCOS_DRIVER_TARGET} ${ENCOS_DRIVER_BASE_TARGET}
        EXPORT encos_driverTargets
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib
        RUNTIME DESTINATION bin
        INCLUDES DESTINATION include
    )

    install(FILES include/encos/export.h DESTINATION include/encos)
    install(FILES include/encos/driver_manager.h DESTINATION include/encos)
    install(FILES include/encos/encos_driver.h DESTINATION include/encos)
    install(FILES include/encos_motor.h DESTINATION include/encos)
    install(FILES include/adapter/base_adapter.h
                   include/adapter/fake_adapter_control.h
             DESTINATION include/encos/adapter)
    install(FILES include/battery/battery.h DESTINATION include/encos/battery)
    install(FILES include/bus/bus.h DESTINATION include/encos/bus)
    install(FILES include/glove/glove.h DESTINATION include/encos/glove)
    install(FILES include/imu/imu.h DESTINATION include/encos/imu)
    install(FILES include/pms/pms.h DESTINATION include/encos/pms)
    install(FILES
        include/platform/delay.h
        include/platform/log.h
        include/platform/os.h
        include/platform/sync.h
        DESTINATION include/encos/platform
    )
    install(FILES
        include/motor/motor.h
        include/motor/types.h
        ${MOTOR_MODEL_GENERATED_HEADER}
        DESTINATION include/encos/motor
    )
    install(FILES include/relay/relay_frame.h DESTINATION include/encos/relay)
    install(FILES
        include/utils/log_writer.h
        include/utils/port.h
        include/utils/thread_priority.h
        DESTINATION include/encos/utils
    )
    if(TARGET ThreadPriorityHelper)
        install(TARGETS ThreadPriorityHelper
            RUNTIME DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR}
        )
        configure_file(
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/set_thread_priority_capability.sh.in"
            "${CMAKE_CURRENT_BINARY_DIR}/set_thread_priority_capability.sh"
            @ONLY
        )
        install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/set_thread_priority_capability.sh"
            DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR}
        )
        install(CODE "
            if(\"\$ENV{DESTDIR}\" STREQUAL \"\")
                execute_process(
                    COMMAND \"\${CMAKE_INSTALL_PREFIX}/${ENCOS_PLUGIN_INSTALL_LIBDIR}/set_thread_priority_capability.sh\" \"\${CMAKE_INSTALL_PREFIX}\"
                    RESULT_VARIABLE _priority_setcap_result
                    OUTPUT_VARIABLE _priority_setcap_output
                    ERROR_VARIABLE _priority_setcap_error
                )
                if(_priority_setcap_output)
                    message(STATUS \"\${_priority_setcap_output}\")
                endif()
                if(_priority_setcap_error)
                    message(STATUS \"\${_priority_setcap_error}\")
                endif()
                if(NOT _priority_setcap_result EQUAL 0)
                    message(FATAL_ERROR \"Failed to set capabilities for ThreadPriorityHelper (exit \${_priority_setcap_result}).\")
                endif()
            endif()
        ")
    endif()
    install(FILES docs/using_guide.md
        DESTINATION share/doc/libencosdriver
    )
    install(DIRECTORY docs/diagrams/
        DESTINATION share/doc/libencosdriver/diagrams
        FILES_MATCHING PATTERN "*.svg"
    )

    install(EXPORT encos_driverTargets
        FILE encos_driverTargets.cmake
        NAMESPACE Encos::
        DESTINATION lib/cmake/encos_driver
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/encos_driverConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/encos_driverConfig.cmake"
        INSTALL_DESTINATION lib/cmake/encos_driver
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/encos_driverConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY AnyNewerVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/encos_driverConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/encos_driverConfigVersion.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EncosPlugins.cmake"
        DESTINATION lib/cmake/encos_driver
    )

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake" @ONLY)

    add_custom_target(uninstall
        COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
endif()

encos_configure_tests()

if(ENCOS_ENABLE_INSTALL)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_SOURCE_GENERATOR "")
    set(CPACK_BINARY_STGZ "OFF")
    set(CPACK_BINARY_TGZ "OFF")
    set(CPACK_BINARY_TZ "OFF")
    set(CPACK_BINARY_TBZ2 "OFF")
    set(CPACK_BINARY_TXZ "OFF")
    set(CPACK_BINARY_RPM "OFF")

    find_program(DPKG_DEB_EXECUTABLE dpkg-deb)
    if(DPKG_DEB_EXECUTABLE)
        set(CPACK_BINARY_DEB "ON")
    else()
        message(WARNING "dpkg-deb not found: DEB package generator will be disabled unless installed in CI.")
        set(CPACK_BINARY_DEB "OFF")
    endif()

    set(CPACK_PACKAGE_NAME "libencosdriver")
    set(CPACK_PACKAGE_VENDOR "Encos")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
        "Encos Motor Driver - C++17 library for Encos motor control")
    set(CPACK_PACKAGE_DESCRIPTION
        "encos_driver is a C++17 library for controlling Encos motors with support for multiple communication protocols such as EtherCAT. Features modular design with plugin mechanism for extensibility.")
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(CPACK_PACKAGE_VERSION
        "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.${PROJECT_VERSION_PATCH}")
    set(CPACK_PACKAGE_CONTACT "zuoqingyu@encos.cn")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Encos Team <zuoqingyu@encos.cn>")
    set(CPACK_DEBIAN_PACKAGE_SECTION "libs")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    if(ENCOS_ENABLE_SPDLOG)
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "libcap-dev, libzstd-dev, libspdlog-dev, libfmt-dev, libstdc++6, libc6")
    else()
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "libcap-dev, libzstd-dev, libstdc++6, libc6")
    endif()
    set(CPACK_DEBIAN_PACKAGE_SUGGESTS "libcap2-bin")
    set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://encos.cn")

    execute_process(
        COMMAND dpkg --print-architecture
        OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
    endif()

    execute_process(
        COMMAND lsb_release -cs
        OUTPUT_VARIABLE DISTRO_CODENAME
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(NOT DISTRO_CODENAME)
        set(DISTRO_CODENAME "unknown")
    endif()

    set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/postinst.in"
        "${CMAKE_CURRENT_BINARY_DIR}/postinst"
        @ONLY
    )
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/postrm.in"
        "${CMAKE_CURRENT_BINARY_DIR}/postrm"
        @ONLY
    )

    install(CODE [[
        if("$ENV{DESTDIR}" STREQUAL "")
            execute_process(COMMAND ldconfig RESULT_VARIABLE _ldconfig_result)
            if(NOT _ldconfig_result EQUAL 0)
                message(WARNING "ldconfig failed with code ${_ldconfig_result}")
            endif()
        endif()
    ]])

    set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_BINARY_DIR}/postinst;${CMAKE_CURRENT_BINARY_DIR}/postrm")
    set(CPACK_PACKAGE_FILE_NAME
        "${CPACK_PACKAGE_NAME}_${CPACK_PACKAGE_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}_${DISTRO_CODENAME}")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_STRIP_FILES TRUE)

    include(CPack)
endif()
