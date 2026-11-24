# SPDX-FileCopyrightText: 2026 Encos
# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.18)
project(encos_portable_smoke LANGUAGES CXX)
find_package(encos_driver REQUIRED)
add_executable(encos-portable-smoke smoke.cc)
target_link_libraries(encos-portable-smoke PRIVATE Encos::encos_driver ${CMAKE_DL_LIBS})
target_compile_features(encos-portable-smoke PRIVATE cxx_std_17)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(encos-portable-smoke PRIVATE stdc++fs)
endif()
set_target_properties(encos-portable-smoke PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
install(TARGETS encos-portable-smoke RUNTIME DESTINATION bin)
