cmake_minimum_required(VERSION 3.18)
project(EncosRelayWsPlugin)

set(CMAKE_CXX_STANDARD 17)

set(RELAYWS_CLIENT_SOURCES client/ix_ws_client.cc)
if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(RELAYWS_CLIENT_SOURCES client/emscripten_ws_client.cc)
endif()

add_library(RelayWsAdapterSupport STATIC
    relay_ws_adapter.cc
    relay_ws_url.cc
    ${RELAYWS_CLIENT_SOURCES}
)
set_target_properties(RelayWsAdapterSupport PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(RelayWsAdapterSupport PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_include_directories(RelayWsAdapterSupport PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../src
)

target_link_libraries(RelayWsAdapterSupport PUBLIC EncosMotorDriverBase)

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # 浏览器 WebSocket 与 /start HTTP 请求分别由 emscripten/websocket.h 和
    # emscripten_fetch 在 C++ 侧完成。
else()
    target_link_libraries(RelayWsAdapterSupport PRIVATE ixwebsocket)
endif()

if(NOT ENCOS_STATIC_MODE)
    add_library(RelayWsPlugin SHARED
        plugin_entry.cc
    )
    encos_configure_plugin_library(RelayWsPlugin)
    target_link_libraries(RelayWsPlugin PRIVATE RelayWsAdapterSupport)
endif()

if(ENCOS_BUILD_TESTS AND NOT (EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten"))
    encos_register_plugin_tests(
        SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/test/relay_ws_adapter_test.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/test/relay_ws_protocol_test.cc
            ${CMAKE_CURRENT_SOURCE_DIR}/test/relay_ws_url_test.cc
        LIBRARIES
            RelayWsAdapterSupport
            ixwebsocket
        INCLUDE_DIRECTORIES
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
endif()
