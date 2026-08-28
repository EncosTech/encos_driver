set(device_files
    "src/battery/battery.cc"
    "src/battery/battery_impl.h"
    "src/glove/glove.cc"
    "src/glove/glove_impl.h"
    "src/imu/imu.cc"
    "src/imu/imu_impl.h"
    "src/pms/pms.cc"
    "src/pms/pms_impl.h"
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
