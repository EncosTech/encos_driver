# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

function(encos_test_device_check out_available out_reason)
    if(NOT ENCOS_BUILD_TESTS)
        set(${out_available} FALSE PARENT_SCOPE)
        set(${out_reason} "TestDevice is only built with ENCOS_BUILD_TESTS=ON" PARENT_SCOPE)
        return()
    endif()
    set(${out_available} TRUE PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)
endfunction()

set(ENCOS_PLUGIN_KIND "device")
set(ENCOS_PLUGIN_CHECK_FUNCTION encos_test_device_check)
set(ENCOS_PLUGIN_DEVICE_TYPE "TestDevice")
set(ENCOS_PLUGIN_DYNAMIC_TARGET "TestDevicePlugin")
set(ENCOS_PLUGIN_STATIC_TARGET "TestDeviceDeviceCore")
set(ENCOS_PLUGIN_STATIC_HEADER "testDevice/test_device.h")
set(ENCOS_PLUGIN_STATIC_FACTORY "encos::MakeTestDeviceDriver")
set(ENCOS_PLUGIN_VISIBLE ON)
