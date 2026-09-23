cmake_minimum_required(VERSION 3.14)
project(example_igh C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_path(IGH_ECRT_INCLUDE_DIR ecrt.h
    PATHS /usr/local/include /usr/include
)
find_library(IGH_ECAT_LIBRARY ethercat
    PATHS /usr/local/lib /usr/lib /usr/lib64
)

if(NOT IGH_ECRT_INCLUDE_DIR OR NOT IGH_ECAT_LIBRARY)
    message(FATAL_ERROR "IGH EtherCAT master headers/libraries not found (ecrt.h / libethercat).")
endif()

add_library(example_igh_core STATIC
    src/demo_timing.c
    src/external_device.c
    src/ethercat_layer.c
    src/motor_layer.c
    src/motor_models.c
)

target_include_directories(example_igh_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${IGH_ECRT_INCLUDE_DIR}
)

target_link_libraries(example_igh_core PUBLIC ${IGH_ECAT_LIBRARY} m)

add_executable(example_igh_demo examples/main.c)
target_link_libraries(example_igh_demo PRIVATE example_igh_core)

if(BUILD_TESTING)
    add_executable(example_igh_tests tests/test_core.c)
    target_link_libraries(example_igh_tests PRIVATE example_igh_core)
    add_test(NAME example_igh_tests COMMAND example_igh_tests)
endif()

if(BUILD_TESTING)
    add_executable(example_igh_recovery_tests tests/test_recovery.c)
    target_link_libraries(example_igh_recovery_tests PRIVATE example_igh_core)
    target_link_options(example_igh_recovery_tests PRIVATE "-Wl,--wrap=ecrt_master_receive")
    target_link_options(example_igh_recovery_tests PRIVATE "-Wl,--wrap=ecrt_domain_process")
    target_link_options(example_igh_recovery_tests PRIVATE "-Wl,--wrap=ecrt_domain_state")
    target_link_options(example_igh_recovery_tests PRIVATE "-Wl,--wrap=ecrt_master_state")
    target_link_options(example_igh_recovery_tests PRIVATE "-Wl,--wrap=ecrt_domain_queue")
    target_link_options(example_igh_recovery_tests PRIVATE "-Wl,--wrap=ecrt_master_send")
    target_link_options(example_igh_recovery_tests PRIVATE "-Wl,--wrap=ecrt_slave_config_state")
    add_test(NAME example_igh_recovery_tests COMMAND example_igh_recovery_tests)
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
