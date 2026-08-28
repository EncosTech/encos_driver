function(encos_define_bool_option option_name default_value description)
    if(DEFINED ${option_name})
        set(_option_value "${${option_name}}")
    else()
        set(_option_value "${default_value}")
    endif()
    set(${option_name} "${_option_value}" CACHE BOOL "${description}" FORCE)
endfunction()

function(encos_configure_options)
    set(_encos_is_emscripten OFF)
    if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(_encos_is_emscripten ON)
    endif()

    if(_encos_is_emscripten AND DEFINED ENCOS_STATIC_MODE AND NOT ENCOS_STATIC_MODE)
        message(FATAL_ERROR "ENCOS_STATIC_MODE must be ON when building with Emscripten.")
    endif()
    if(_encos_is_emscripten AND ENCOS_BUILD_TESTS)
        message(FATAL_ERROR "ENCOS_BUILD_TESTS must be OFF when building with Emscripten.")
    endif()

    encos_define_bool_option(
        ENCOS_ENABLE_PYTHON
        ON
        "Enable Python-dependent tests and generated source refresh")
    encos_define_bool_option(
        ENCOS_BUILD_WASM_BINDINGS
        OFF
        "Build the Emscripten WASM bindings target")
    encos_define_bool_option(
        ENCOS_BUILD_WASM_TESTS
        OFF
        "Build and run Node.js tests for the Emscripten WASM bindings")
    encos_define_bool_option(
        ENCOS_SKIP_GENERATED_SHA1_CHECK
        OFF
        "Skip generated source CSV SHA1 checks when Python is disabled")

    if(ENCOS_BUILD_WASM_BINDINGS AND NOT _encos_is_emscripten)
        message(FATAL_ERROR "ENCOS_BUILD_WASM_BINDINGS is only supported with Emscripten.")
    endif()
    if(ENCOS_BUILD_WASM_TESTS AND NOT ENCOS_BUILD_WASM_BINDINGS)
        message(FATAL_ERROR "ENCOS_BUILD_WASM_TESTS requires ENCOS_BUILD_WASM_BINDINGS=ON.")
    endif()

    if(ENCOS_BUILD_TESTS AND NOT ENCOS_ENABLE_PYTHON)
        message(FATAL_ERROR "ENCOS_BUILD_TESTS requires ENCOS_ENABLE_PYTHON=ON.")
    endif()

    set(_encos_default_spdlog ON)
    if(_encos_is_emscripten)
        set(_encos_default_spdlog OFF)
    endif()
    encos_define_bool_option(
        ENCOS_ENABLE_SPDLOG
        ${_encos_default_spdlog}
        "Use spdlog for EncosMotorDriver logging")

    set(_encos_default_static OFF)
    if(_encos_is_emscripten)
        set(_encos_default_static ON)
    endif()
    encos_define_bool_option(
        ENCOS_STATIC_MODE
        ${_encos_default_static}
        "Build EncosMotorDriver as a static library with built-in adapters")

    set(_encos_default_install OFF)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT ENCOS_STATIC_MODE)
        set(_encos_default_install ON)
    endif()
    encos_define_bool_option(
        ENCOS_ENABLE_INSTALL
        ${_encos_default_install}
        "Enable install and packaging rules")

    if(ENCOS_ENABLE_INSTALL AND
       (NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR ENCOS_STATIC_MODE))
        message(FATAL_ERROR
            "ENCOS_ENABLE_INSTALL is only supported on Linux dynamic builds.")
    endif()
endfunction()
