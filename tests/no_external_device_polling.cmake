# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT

set(device_files
    "src/device/battery/battery.cc"
    "src/device/battery/battery_impl.h"
    "src/device/glove/glove.cc"
    "src/device/glove/glove_impl.h"
    "src/device/imu/imu.cc"
    "src/device/imu/imu_impl.h"
    "src/device/pms/pms.cc"
    "src/device/pms/pms_impl.h"
)

set(forbidden_tokens
    "std::thread"
    "emscripten_set_interval"
    "emscripten_clear_interval"
    "DrainUnknownMessages"
    "update_thread"
    "update_timer"
)

foreach(relative_path IN LISTS device_files)
    file(READ "${ENCOS_SOURCE_DIR}/${relative_path}" contents)
    foreach(token IN LISTS forbidden_tokens)
        string(FIND "${contents}" "${token}" position)
        if(NOT position EQUAL -1)
            message(FATAL_ERROR "${relative_path} contains forbidden polling token: ${token}")
        endif()
    endforeach()
endforeach()
