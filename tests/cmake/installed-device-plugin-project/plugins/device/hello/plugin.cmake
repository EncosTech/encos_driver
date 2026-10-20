# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

# 插件清单（文件 (2)）：安装消费模式只需检查函数、类型名与动态目标名。
function(encos_hello_check out_available out_reason)
    set(${out_available} TRUE PARENT_SCOPE)
    set(${out_reason} "" PARENT_SCOPE)
endfunction()

set(ENCOS_PLUGIN_CHECK_FUNCTION encos_hello_check)
set(ENCOS_PLUGIN_DEVICE_TYPE "Hello")
set(ENCOS_PLUGIN_DYNAMIC_TARGET "HelloPlugin")
set(ENCOS_PLUGIN_VISIBLE ON)
