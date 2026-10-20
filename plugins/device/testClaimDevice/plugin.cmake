# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

function(encos_test_claim_device_check out_available out_reason)
    if(NOT ENCOS_BUILD_TESTS)
        set(${out_available} FALSE PARENT_SCOPE)
        set(${out_reason} "TestClaimDevice is only built with ENCOS_BUILD_TESTS=ON" PARENT_SCOPE)
        return()
    endif()
    set(${out_available} TRUE PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)
endfunction()

set(ENCOS_PLUGIN_KIND "device")
set(ENCOS_PLUGIN_CHECK_FUNCTION encos_test_claim_device_check)
set(ENCOS_PLUGIN_DEVICE_TYPE "TestClaimDevice")
set(ENCOS_PLUGIN_DYNAMIC_TARGET "TestClaimDevicePlugin")
set(ENCOS_PLUGIN_STATIC_TARGET "TestClaimDeviceCore")
set(ENCOS_PLUGIN_STATIC_HEADER "testClaimDevice/test_claim_device.h")
set(ENCOS_PLUGIN_STATIC_FACTORY "encos::MakeTestClaimDeviceDriver")
set(ENCOS_PLUGIN_VISIBLE ON)
