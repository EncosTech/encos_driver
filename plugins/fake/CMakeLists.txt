cmake_minimum_required(VERSION 3.18)
project(EncosFakePlugin)

set(CMAKE_CXX_STANDARD 17)

add_library(FakeAdapterSupport STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/fake_adapter.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/fake_protocol.cc
)
set_target_properties(FakeAdapterSupport PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(FakeAdapterSupport PUBLIC
    EncosMotorDriverBase
)
target_include_directories(FakeAdapterSupport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_include_directories(FakeAdapterSupport PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

if(NOT ENCOS_STATIC_MODE)
    add_library(FakePlugin SHARED
        ${CMAKE_CURRENT_SOURCE_DIR}/plugin_entry.cc
    )
    encos_configure_plugin_library(FakePlugin)
    target_link_libraries(FakePlugin PRIVATE FakeAdapterSupport)
endif()

if(ENCOS_BUILD_TESTS)
    set(_fake_test_sources
        ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_adapter_test.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_adapter_control_test.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_protocol_test.cc
    )
    set(_fake_test_dependencies "")
    if(NOT ENCOS_STATIC_MODE)
        list(APPEND _fake_test_sources
            ${CMAKE_CURRENT_SOURCE_DIR}/test/fake_adapter_control_dynamic_test.cc
        )
        list(APPEND _fake_test_dependencies FakePlugin)
    endif()
    encos_register_plugin_tests(
        SOURCES ${_fake_test_sources}
        LIBRARIES FakeAdapterSupport
        DEPENDENCIES ${_fake_test_dependencies}
    )
endif()
