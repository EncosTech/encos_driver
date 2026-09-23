cmake_minimum_required(VERSION 3.14)
project(example_soem C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if(NOT TARGET soem)
    add_subdirectory(external/SOEM)
endif()

add_library(example_soem_core STATIC
    src/demo_timing.c
    src/external_device.c
    src/ethercat_layer.c
    src/motor_layer.c
    src/motor_models.c
)

target_include_directories(example_soem_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/SOEM/include
)

target_link_libraries(example_soem_core PUBLIC soem m)

add_executable(example_soem_demo examples/main.c)
target_link_libraries(example_soem_demo PRIVATE example_soem_core)

if(BUILD_TESTING)
    add_executable(example_soem_tests tests/test_core.c)
    target_link_libraries(example_soem_tests PRIVATE example_soem_core)
    add_test(NAME example_soem_tests COMMAND example_soem_tests)
endif()

if(BUILD_TESTING)
    add_executable(example_soem_recovery_tests tests/test_recovery.c)
    target_link_libraries(example_soem_recovery_tests PRIVATE example_soem_core)
    target_link_options(example_soem_recovery_tests PRIVATE "-Wl,--wrap=ecx_send_processdata")
    target_link_options(example_soem_recovery_tests PRIVATE "-Wl,--wrap=ecx_receive_processdata")
    target_link_options(example_soem_recovery_tests PRIVATE "-Wl,--wrap=ecx_readstate")
    target_link_options(example_soem_recovery_tests PRIVATE "-Wl,--wrap=ecx_writestate")
    target_link_options(example_soem_recovery_tests PRIVATE "-Wl,--wrap=ecx_statecheck")
    target_link_options(example_soem_recovery_tests PRIVATE "-Wl,--wrap=ecx_reconfig_slave")
    target_link_options(example_soem_recovery_tests PRIVATE "-Wl,--wrap=ecx_recover_slave")
    add_test(NAME example_soem_recovery_tests COMMAND example_soem_recovery_tests)
endif()

add_custom_target(format-check
    COMMAND bash scripts/run_clang_format.sh --check
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM
)
add_custom_target(tidy
    COMMAND bash scripts/run_clang_tidy.sh ${CMAKE_BINARY_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    VERBATIM
)
