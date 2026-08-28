# EncosPlugins.cmake
#
# 提供 EncosMotorDriver 插件注册、发现与注册代码生成的 CMake API。
#
# 公开函数：
#   add_emd_plugin(<name> <path> [OPTIONAL])
#   encos_discover_builtin_plugins(<plugins_dir>)
#   encos_generate_plugin_registry(<output_dir>)
#
# 辅助函数（插件 CMakeLists 可用）：
#   encos_configure_plugin_library(<target>)
#
# 要求：调用 add_emd_plugin 前必须已定义 EncosMotorDriver 与 EncosMotorDriverBase 目标。

function(encos_configure_plugin_library target)
    target_compile_definitions(${target} PRIVATE ENCOS_PLUGIN_EXPORTS)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
endfunction()

# 校验动态插件目标是 SHARED_LIBRARY 或 MODULE_LIBRARY
function(_encos_validate_dynamic_target plugin_name expected_target)
    if(NOT TARGET ${expected_target})
        message(FATAL_ERROR
            "add_emd_plugin: plugin '${plugin_name}' did not create expected target "
            "'${expected_target}'")
    endif()

    get_target_property(_expected_target_type ${expected_target} TYPE)
    if(NOT _expected_target_type MATCHES "^(SHARED|MODULE)_LIBRARY$")
        message(FATAL_ERROR
            "add_emd_plugin: plugin '${plugin_name}' dynamic target "
            "'${expected_target}' has type '${_expected_target_type}', "
            "but must be SHARED_LIBRARY or MODULE_LIBRARY")
    endif()
endfunction()

function(add_emd_plugin plugin_name plugin_path)
    # 解析 OPTIONAL 标志
    set(_optional FALSE)
    if(ARGC GREATER 2 AND "${ARGV2}" STREQUAL "OPTIONAL")
        set(_optional TRUE)
    endif()

    get_property(_installed_mode GLOBAL PROPERTY _ENCOS_INSTALLED_PACKAGE_MODE)
    if(_installed_mode)
        set(_installed_mode TRUE)
    else()
        set(_installed_mode FALSE)
    endif()

    # 校验插件名
    if(NOT plugin_name MATCHES "^[A-Za-z0-9_-]+$")
        message(FATAL_ERROR "add_emd_plugin: invalid plugin name '${plugin_name}'")
    endif()

    # 规范化路径
    get_filename_component(_abs_path "${plugin_path}" ABSOLUTE
        BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    # 重复检测
    get_property(_registered_names GLOBAL PROPERTY _ENCOS_REGISTERED_PLUGIN_NAMES)
    get_property(_registered_paths GLOBAL PROPERTY _ENCOS_REGISTERED_PLUGIN_PATHS)
    list(FIND _registered_names "${plugin_name}" _existing_index)
    if(_existing_index GREATER -1)
        list(GET _registered_paths ${_existing_index} _existing_path)
        if(NOT _existing_path STREQUAL _abs_path)
            message(FATAL_ERROR
                "add_emd_plugin: plugin '${plugin_name}' registered from conflicting paths:\n"
                "  previous: ${_existing_path}\n"
                "  current:  ${_abs_path}")
        endif()
        # 同名同路径幂等返回
        return()
    endif()

    # 加载清单
    set(_manifest "${_abs_path}/plugin.cmake")
    if(NOT EXISTS "${_manifest}")
        if(_optional)
            message(STATUS "Skipping plugin '${plugin_name}': missing plugin.cmake at ${_abs_path}")
            return()
        else()
            message(FATAL_ERROR
                "add_emd_plugin: plugin '${plugin_name}' missing plugin.cmake at ${_abs_path}")
        endif()
    endif()

    # 在隔离作用域中加载清单字段
    set(ENCOS_PLUGIN_CHECK_FUNCTION "")
    set(ENCOS_PLUGIN_ADAPTER_TYPE "")
    set(ENCOS_PLUGIN_DYNAMIC_TARGET "")
    set(ENCOS_PLUGIN_STATIC_TARGET "")
    set(ENCOS_PLUGIN_STATIC_HEADER "")
    set(ENCOS_PLUGIN_STATIC_FACTORY "")
    set(ENCOS_PLUGIN_VISIBLE ON)
    include("${_manifest}")

    set(_required_fields
        ENCOS_PLUGIN_CHECK_FUNCTION
        ENCOS_PLUGIN_ADAPTER_TYPE
        ENCOS_PLUGIN_DYNAMIC_TARGET
        ENCOS_PLUGIN_STATIC_TARGET
        ENCOS_PLUGIN_STATIC_HEADER
        ENCOS_PLUGIN_STATIC_FACTORY)
    foreach(_field IN LISTS _required_fields)
        if("${${_field}}" STREQUAL "")
            if(_optional)
                message(STATUS "Skipping plugin '${plugin_name}': ${_field} is empty")
                return()
            else()
                message(FATAL_ERROR
                    "add_emd_plugin: plugin '${plugin_name}' has empty ${_field}")
            endif()
        endif()
    endforeach()

    # 校验适配器类型可作为 C++ 标识符
    if(NOT ENCOS_PLUGIN_ADAPTER_TYPE MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR
            "add_emd_plugin: plugin '${plugin_name}' has invalid adapter type "
            "'${ENCOS_PLUGIN_ADAPTER_TYPE}' (must be a valid C++ identifier)")
    endif()

    # 校验适配器类型未被其他插件名占用
    get_property(_registered_adapter_types GLOBAL PROPERTY _ENCOS_REGISTERED_ADAPTER_TYPES)
    if("${ENCOS_PLUGIN_ADAPTER_TYPE}" IN_LIST _registered_adapter_types)
        message(FATAL_ERROR
            "add_emd_plugin: plugin '${plugin_name}' reuses adapter type "
            "'${ENCOS_PLUGIN_ADAPTER_TYPE}' which is already registered")
    endif()

    # 校验可见性为合法布尔值
    set(_encos_valid_bool_values ON OFF TRUE FALSE 1 0 YES NO)
    if(NOT ENCOS_PLUGIN_VISIBLE IN_LIST _encos_valid_bool_values)
        message(FATAL_ERROR
            "add_emd_plugin: plugin '${plugin_name}' has invalid ENCOS_PLUGIN_VISIBLE "
            "'${ENCOS_PLUGIN_VISIBLE}' (must be a boolean)")
    endif()

    if(NOT COMMAND ${ENCOS_PLUGIN_CHECK_FUNCTION})
        if(_optional)
            message(STATUS "Skipping plugin '${plugin_name}': "
                "check function '${ENCOS_PLUGIN_CHECK_FUNCTION}' not defined")
            return()
        else()
            message(FATAL_ERROR
                "add_emd_plugin: plugin '${plugin_name}' check function "
                "'${ENCOS_PLUGIN_CHECK_FUNCTION}' not defined")
        endif()
    endif()

    # 调用检查函数并校验返回的严格布尔值与 reason 一致性
    set(_available FALSE)
    set(_reason "")
    cmake_language(CALL ${ENCOS_PLUGIN_CHECK_FUNCTION} _available _reason)

    if(NOT _available IN_LIST _encos_valid_bool_values)
        message(FATAL_ERROR
            "add_emd_plugin: plugin '${plugin_name}' check function "
            "'${ENCOS_PLUGIN_CHECK_FUNCTION}' returned non-boolean available value "
            "'${_available}'")
    endif()

    if(_available AND NOT "${_reason}" STREQUAL "")
        message(FATAL_ERROR
            "add_emd_plugin: plugin '${plugin_name}' check function "
            "'${ENCOS_PLUGIN_CHECK_FUNCTION}' returned available=TRUE but non-empty reason")
    endif()
    if(NOT _available AND "${_reason}" STREQUAL "")
        set(_reason "unknown reason")
    endif()

    if(NOT _available)
        if(_optional)
            message(STATUS "Skipping plugin '${plugin_name}': ${_reason}")
            return()
        else()
            message(FATAL_ERROR
                "add_emd_plugin: plugin '${plugin_name}' is unavailable: ${_reason}")
        endif()
    endif()

    # 记录注册信息（安装消费模式同样进行重复/适配器类型检测）
    set_property(GLOBAL APPEND PROPERTY _ENCOS_REGISTERED_PLUGIN_NAMES "${plugin_name}")
    set_property(GLOBAL APPEND PROPERTY _ENCOS_REGISTERED_PLUGIN_PATHS "${_abs_path}")
    set_property(GLOBAL APPEND PROPERTY _ENCOS_REGISTERED_ADAPTER_TYPES "${ENCOS_PLUGIN_ADAPTER_TYPE}")

    if(_installed_mode)
        # 安装消费模式：只处理动态外部插件，不修改已导入主库目标
        add_subdirectory("${_abs_path}" "${CMAKE_CURRENT_BINARY_DIR}/plugins/${plugin_name}")

        _encos_validate_dynamic_target("${plugin_name}" "${ENCOS_PLUGIN_DYNAMIC_TARGET}")

        set_target_properties(${ENCOS_PLUGIN_DYNAMIC_TARGET} PROPERTIES
            OUTPUT_NAME "${ENCOS_PLUGIN_ADAPTER_TYPE}Plugin"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/plugins"
        )

        # 默认安装外部动态插件到驱动插件目录；消费者可通过 ENCOS_ENABLE_INSTALL=OFF 关闭
        if(NOT ENCOS_ENABLE_INSTALL STREQUAL "OFF")
            install(TARGETS ${ENCOS_PLUGIN_DYNAMIC_TARGET}
                LIBRARY DESTINATION "${ENCOS_PLUGIN_INSTALL_DIR}"
            )
        endif()
    else()
        # 源码树模式：执行插件子目录并集成到主库
        add_subdirectory("${_abs_path}" "${CMAKE_BINARY_DIR}/plugins/${plugin_name}")

        # 校验当前模式目标存在
        if(ENCOS_STATIC_MODE)
            set(_expected_target "${ENCOS_PLUGIN_STATIC_TARGET}")
        else()
            set(_expected_target "${ENCOS_PLUGIN_DYNAMIC_TARGET}")
        endif()
        if(NOT TARGET ${_expected_target})
            message(FATAL_ERROR
                "add_emd_plugin: plugin '${plugin_name}' did not create expected target "
                "'${_expected_target}'")
        endif()

        # 动态模式下要求目标为 SHARED/MODULE，确保会产出可加载的插件库
        if(NOT ENCOS_STATIC_MODE)
            _encos_validate_dynamic_target("${plugin_name}" "${_expected_target}")
        endif()

        # 维护目标属性，供生成头使用
        get_target_property(_count EncosMotorDriver ENCOS_PLUGIN_COUNT)
        if(NOT _count OR _count STREQUAL "_count-NOTFOUND")
            set(_count 0)
        endif()
        math(EXPR _count "${_count} + 1")
        set_target_properties(EncosMotorDriver PROPERTIES ENCOS_PLUGIN_COUNT "${_count}")

        set_property(TARGET EncosMotorDriver APPEND PROPERTY ENCOS_ENABLED_PLUGINS "${plugin_name}")
        set_property(TARGET EncosMotorDriver APPEND PROPERTY ENCOS_PLUGIN_ADAPTER_TYPES "${ENCOS_PLUGIN_ADAPTER_TYPE}")
        set_property(TARGET EncosMotorDriver APPEND PROPERTY ENCOS_PLUGIN_STATIC_HEADERS "${ENCOS_PLUGIN_STATIC_HEADER}")
        set_property(TARGET EncosMotorDriver APPEND PROPERTY ENCOS_PLUGIN_STATIC_FACTORIES "${ENCOS_PLUGIN_STATIC_FACTORY}")
        set_property(TARGET EncosMotorDriver APPEND PROPERTY ENCOS_PLUGIN_VISIBLE "${ENCOS_PLUGIN_VISIBLE}")

        if(NOT ENCOS_PLUGIN_VISIBLE)
            get_target_property(_hidden_count EncosMotorDriver ENCOS_HIDDEN_PLUGIN_COUNT)
            if(NOT _hidden_count OR _hidden_count STREQUAL "_hidden_count-NOTFOUND")
                set(_hidden_count 0)
            endif()
            math(EXPR _hidden_count "${_hidden_count} + 1")
            set_target_properties(EncosMotorDriver PROPERTIES ENCOS_HIDDEN_PLUGIN_COUNT "${_hidden_count}")
            set_property(TARGET EncosMotorDriver APPEND PROPERTY ENCOS_HIDDEN_PLUGIN_TYPES "\"${ENCOS_PLUGIN_ADAPTER_TYPE}\"")
        endif()

        # 生成静态加载器片段
        set(_static_include "#include \"${ENCOS_PLUGIN_STATIC_HEADER}\"")
        set(_static_wrapper
            "encos::BaseAdapter* EncosStaticCreate_${ENCOS_PLUGIN_ADAPTER_TYPE}(const std::string& interface_name, const std::string& logger_name, encos::LogLevel log_level) {\n    return ${ENCOS_PLUGIN_STATIC_FACTORY}(interface_name, logger_name, log_level)\;\n}")
        if(ENCOS_PLUGIN_VISIBLE)
            set(_static_def
                "    {\"${ENCOS_PLUGIN_ADAPTER_TYPE}\", &EncosStaticCreate_${ENCOS_PLUGIN_ADAPTER_TYPE}, true}")
        else()
            set(_static_def
                "    {\"${ENCOS_PLUGIN_ADAPTER_TYPE}\", &EncosStaticCreate_${ENCOS_PLUGIN_ADAPTER_TYPE}, false}")
        endif()

        set_property(TARGET EncosMotorDriver APPEND PROPERTY ENCOS_STATIC_INCLUDES "${_static_include}")
        set_property(TARGET EncosMotorDriver APPEND PROPERTY ENCOS_STATIC_WRAPPERS "${_static_wrapper}")
        set_property(TARGET EncosMotorDriver APPEND PROPERTY ENCOS_STATIC_DEFINITIONS "${_static_def}")

        # 目标集成
        if(ENCOS_STATIC_MODE)
            target_link_libraries(EncosMotorDriver PUBLIC ${ENCOS_PLUGIN_STATIC_TARGET})
        else()
            add_dependencies(EncosMotorDriver ${ENCOS_PLUGIN_DYNAMIC_TARGET})
            set_target_properties(${ENCOS_PLUGIN_DYNAMIC_TARGET} PROPERTIES
                OUTPUT_NAME "${ENCOS_PLUGIN_ADAPTER_TYPE}Plugin"
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins"
            )
            if(ENCOS_ENABLE_INSTALL)
                install(TARGETS ${ENCOS_PLUGIN_DYNAMIC_TARGET}
                    LIBRARY DESTINATION ${ENCOS_PLUGIN_INSTALL_LIBDIR}
                )
            endif()
        endif()
    endif()

    message(STATUS "Registered plugin '${plugin_name}' (${ENCOS_PLUGIN_ADAPTER_TYPE}) from ${_abs_path}")
endfunction()

function(encos_discover_builtin_plugins plugins_dir)
    if(DEFINED ENCOS_PLUGINS_LIST)
        # 显式模式：黑名单被忽略
        foreach(_plugin_name IN LISTS ENCOS_PLUGINS_LIST)
            add_emd_plugin("${_plugin_name}" "${plugins_dir}/${_plugin_name}")
        endforeach()
        return()
    endif()

    # 自动扫描模式
    file(GLOB _plugin_manifests CONFIGURE_DEPENDS "${plugins_dir}/*/plugin.cmake")
    set(_plugin_names "")
    foreach(_manifest IN LISTS _plugin_manifests)
        get_filename_component(_plugin_dir "${_manifest}" DIRECTORY)
        get_filename_component(_plugin_name "${_plugin_dir}" NAME)
        if(_plugin_name MATCHES "^[._]")
            continue()
        endif()
        list(APPEND _plugin_names "${_plugin_name}")
    endforeach()
    list(SORT _plugin_names)

    foreach(_plugin_name IN LISTS _plugin_names)
        if(DEFINED ENCOS_PLUGINS_BLACKLIST AND "${_plugin_name}" IN_LIST ENCOS_PLUGINS_BLACKLIST)
            message(STATUS "Skipping blacklisted plugin '${_plugin_name}'")
            continue()
        endif()
        add_emd_plugin("${_plugin_name}" "${plugins_dir}/${_plugin_name}" OPTIONAL)
    endforeach()
endfunction()

function(encos_generate_plugin_registry output_dir)
    if(ENCOS_BUILD_TESTS)
        get_target_property(_enabled_plugins EncosMotorDriver ENCOS_ENABLED_PLUGINS)
        if(NOT _enabled_plugins OR NOT "fake" IN_LIST _enabled_plugins)
            message(FATAL_ERROR
                "ENCOS_BUILD_TESTS=ON requires the 'Fake' plugin to be enabled. "
                "Add 'fake' to ENCOS_PLUGINS_LIST or remove it from ENCOS_PLUGINS_BLACKLIST.")
        endif()
    endif()

    set(_static_registry_header "${output_dir}/encos_static_plugin_registry.generated.h")
    set(_hidden_types_header "${output_dir}/encos_hidden_plugin_types.generated.h")

    # 确保未注册任何插件时生成器表达式也能得到 0 而不是空字符串
    get_target_property(_plugin_count EncosMotorDriver ENCOS_PLUGIN_COUNT)
    if(NOT _plugin_count OR _plugin_count STREQUAL "_plugin_count-NOTFOUND")
        set_target_properties(EncosMotorDriver PROPERTIES ENCOS_PLUGIN_COUNT "0")
    endif()
    get_target_property(_hidden_count EncosMotorDriver ENCOS_HIDDEN_PLUGIN_COUNT)
    if(NOT _hidden_count OR _hidden_count STREQUAL "_hidden_count-NOTFOUND")
        set_target_properties(EncosMotorDriver PROPERTIES ENCOS_HIDDEN_PLUGIN_COUNT "0")
    endif()

    set(_static_registry_content
"#pragma once

#include <array>
#include <string>
#include <string_view>

#include \"adapter/base_adapter.h\"
#include \"platform/log.h\"

$<JOIN:$<TARGET_PROPERTY:EncosMotorDriver,ENCOS_STATIC_INCLUDES>,\n>

namespace encos {

namespace {

using StaticCreateFunc = encos::BaseAdapter* (*)(const std::string&, const std::string&, encos::LogLevel);

struct StaticPluginDefinition {
    std::string_view adapter_type;
    StaticCreateFunc create;
    bool include_in_available_types;
};

$<JOIN:$<TARGET_PROPERTY:EncosMotorDriver,ENCOS_STATIC_WRAPPERS>,\n\n>

} // namespace

inline const std::array<StaticPluginDefinition, $<TARGET_PROPERTY:EncosMotorDriver,ENCOS_PLUGIN_COUNT>>& GetStaticPluginDefinitions() {
    static const std::array<StaticPluginDefinition, $<TARGET_PROPERTY:EncosMotorDriver,ENCOS_PLUGIN_COUNT>> definitions = {{
$<JOIN:$<TARGET_PROPERTY:EncosMotorDriver,ENCOS_STATIC_DEFINITIONS>,\,\n>
    }};
    return definitions;
}

} // namespace encos
")

    file(GENERATE OUTPUT "${_static_registry_header}" CONTENT "${_static_registry_content}")

    set(_hidden_types_content
"#pragma once

#include <array>
#include <string_view>

namespace encos {
constexpr std::array<std::string_view, $<TARGET_PROPERTY:EncosMotorDriver,ENCOS_HIDDEN_PLUGIN_COUNT>> kHiddenAdapterTypes = {
$<JOIN:$<TARGET_PROPERTY:EncosMotorDriver,ENCOS_HIDDEN_PLUGIN_TYPES>,\,\n>
};
} // namespace encos
")

    file(GENERATE OUTPUT "${_hidden_types_header}" CONTENT "${_hidden_types_content}")
endfunction()
