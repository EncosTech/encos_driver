# Keep the existing Base DLL exports while owning the shared codec in plugins/utils.
target_sources(${ENCOS_DRIVER_BASE_TARGET} PRIVATE relay_frame.cc)
target_include_directories(${ENCOS_DRIVER_BASE_TARGET} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../..>)
if(ENCOS_ENABLE_INSTALL)
    install(FILES relay_frame.h DESTINATION include/encos/utils/emr1)
endif()
if(ENCOS_BUILD_TESTS)
    encos_register_plugin_tests(
        SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/test/relay_frame_test.cc
        LIBRARIES ${ENCOS_DRIVER_BASE_TARGET})
endif()
