# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

if(ENCOS_BUILD_NODE_BINDINGS)
    if(EMSCRIPTEN OR ENCOS_STATIC_MODE)
        message(FATAL_ERROR "Node bindings require a native dynamic build")
    endif()
    if(NOT TARGET FakeAdapterSupport)
        message(FATAL_ERROR "Node bindings require the fake plugin in ENCOS_PLUGINS_LIST")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_path(ENCOS_NODE_INCLUDE_DIR node_api.h PATH_SUFFIXES node nodejs/src
        DOC "Directory containing Node-API headers")
    if(NOT ENCOS_NODE_INCLUDE_DIR)
        message(FATAL_ERROR "Set ENCOS_NODE_INCLUDE_DIR to the Node.js include/node directory")
    endif()
    set(_node_generated "${CMAKE_CURRENT_BINARY_DIR}/generated/node")
    add_custom_command(
        OUTPUT "${_node_generated}/node_c_api.h" "${_node_generated}/node_dispatch.inc"
        COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/src/node/generate_dispatch.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/wasm/wasm_c_api.cc" "${_node_generated}"
        DEPENDS src/node/generate_dispatch.py src/wasm/wasm_c_api.cc
        VERBATIM)
    add_library(encosdriver_node MODULE
        src/node/node_binding.cc src/node/native_api.cc src/node/native_devices.cc
        src/node/native_fake.cc src/node/native_utilities.cc src/wasm/wasm_runtime.cc src/wasm/wasm_c_api.cc
        "${_node_generated}/node_c_api.h" "${_node_generated}/node_dispatch.inc")
    target_include_directories(encosdriver_node PRIVATE
        "${ENCOS_NODE_INCLUDE_DIR}" "${_node_generated}" "${ENCOS_GENERATED_INCLUDE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(encosdriver_node PRIVATE NAPI_VERSION=8 NODE_GYP_MODULE_NAME=encosdriver)
    target_link_libraries(encosdriver_node PRIVATE ${ENCOS_DRIVER_TARGET} FakeAdapterSupport)
    set_target_properties(encosdriver_node PROPERTIES PREFIX "" SUFFIX ".node"
        OUTPUT_NAME encosdriver
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/native"
        BUILD_WITH_INSTALL_RPATH FALSE INSTALL_RPATH "$ORIGIN/../runtime/lib")
    if(WIN32)
        set(ENCOS_NODE_IMPORT_LIBRARY "" CACHE FILEPATH "Node.js import library for target toolchain")
        if(NOT ENCOS_NODE_IMPORT_LIBRARY)
            message(FATAL_ERROR "Set ENCOS_NODE_IMPORT_LIBRARY to the MinGW Node.js import library")
        endif()
        target_link_libraries(encosdriver_node PRIVATE "${ENCOS_NODE_IMPORT_LIBRARY}")
    endif()
endif()
