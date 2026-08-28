function(encos_verify_generated_csv_sha1 csv_file)
    set(_generated_files ${ARGN})
    if(NOT EXISTS "${csv_file}")
        message(FATAL_ERROR "Generated source CSV does not exist: ${csv_file}")
    endif()
    if(NOT _generated_files)
        message(FATAL_ERROR "encos_verify_generated_csv_sha1 requires generated files.")
    endif()

    file(SHA1 "${csv_file}" _csv_sha1)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${csv_file}"
        ${_generated_files}
    )

    foreach(_generated_file IN LISTS _generated_files)
        if(NOT EXISTS "${_generated_file}")
            message(FATAL_ERROR
                "Generated file is required when ENCOS_ENABLE_PYTHON=OFF: ${_generated_file}")
        endif()

        file(STRINGS "${_generated_file}" _generated_first_line LIMIT_COUNT 1 ENCODING UTF-8)
        if(NOT _generated_first_line MATCHES
           "^// Auto-generated from .+\\.csv\\. CSV SHA1: ([0-9a-f]+)$")
            message(FATAL_ERROR
                "Generated file does not record CSV SHA1 on the first line: ${_generated_file}")
        endif()
        set(_generated_sha1 "${CMAKE_MATCH_1}")
        string(LENGTH "${_generated_sha1}" _generated_sha1_length)
        if(NOT _generated_sha1_length EQUAL 40)
            message(FATAL_ERROR
                "Generated file records an invalid CSV SHA1 on the first line: ${_generated_file}")
        endif()

        if(NOT _generated_sha1 STREQUAL _csv_sha1)
            message(FATAL_ERROR
                "Generated file SHA1 mismatch for ${_generated_file}: "
                "expected ${_csv_sha1}, found ${_generated_sha1}. "
                "Reconfigure with ENCOS_ENABLE_PYTHON=ON to regenerate, "
                "or set ENCOS_SKIP_GENERATED_SHA1_CHECK=ON to bypass this check.")
        endif()
    endforeach()
endfunction()
