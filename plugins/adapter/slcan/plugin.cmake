# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

function(encos_slcan_check out_available out_reason)
    if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        set(${out_available} FALSE PARENT_SCOPE)
        set(${out_reason} "Slcan plugin is not supported on Emscripten" PARENT_SCOPE)
        return()
    endif()
    set(${out_available} TRUE PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)
endfunction()

set(ENCOS_PLUGIN_CHECK_FUNCTION encos_slcan_check)
set(ENCOS_PLUGIN_ADAPTER_TYPE "Slcan")
set(ENCOS_PLUGIN_DYNAMIC_TARGET "SlcanPlugin")
set(ENCOS_PLUGIN_STATIC_TARGET "SlcanPluginCore")
set(ENCOS_PLUGIN_STATIC_HEADER "slcan/slcan_adapter.h")
set(ENCOS_PLUGIN_STATIC_FACTORY "encos::SlcanAdapter::Create")
set(ENCOS_PLUGIN_ENUMERATE_FUNCTION "encos::EnumerateSlcanInterfaces")
set(ENCOS_PLUGIN_STATIC_FACTORY_EXTEND "encos::SlcanAdapter::CreateWithExtend")
set(ENCOS_PLUGIN_VISIBLE ON)
