# This software is dual-licensed under GPLv3 and a commercial
# license. See the file LICENSE.md distributed with this software for
# full license information.

target_sources(soem PRIVATE
  osal/win32/osal.c
  osal/win32/osal_defs.h
  oshw/win32/oshw.c
  oshw/win32/oshw.h
  oshw/win32/nicdrv.c
  oshw/win32/nicdrv.h
)

target_include_directories(soem PUBLIC
  $<BUILD_INTERFACE:${SOEM_SOURCE_DIR}/osal/win32>
  $<BUILD_INTERFACE:${SOEM_SOURCE_DIR}/oshw/win32>
  $<BUILD_INTERFACE:${SOEM_SOURCE_DIR}/oshw/win32/wpcap/Include>
  $<INSTALL_INTERFACE:include/soem>
)

target_compile_options(soem PUBLIC
  $<$<C_COMPILER_ID:GNU>:-std=c11>
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  include(CheckCSourceCompiles)
  check_c_source_compiles([=[
#include <_mingw.h>
#ifndef _UCRT
#error target uses msvcrt, not UCRT
#endif
int main(void) { return 0; }
]=] SOEM_MINGW_USES_UCRT)
  if(SOEM_MINGW_USES_UCRT)
    target_compile_definitions(soem PUBLIC _UCRT)
    target_link_libraries(soem PUBLIC ucrt)
  endif()
endif()

foreach(target IN ITEMS
    soem
    ec_sample
    eepromtool
    eni_test
    eoe_test
    firm_update
    simple_ng
    slaveinfo)
  if (TARGET ${target})
    target_compile_options(${target} PRIVATE
      $<$<C_COMPILER_ID:MSVC>:
      /D _CRT_SECURE_NO_WARNINGS
      /W3
      >
      $<$<C_COMPILER_ID:GNU>:
      -Wall
      -Wextra
      -Wno-unused-parameter
      >
    )
  endif()
endforeach()


if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(WPCAP_LIB_PATH ${SOEM_SOURCE_DIR}/oshw/win32/wpcap/Lib/x64)
elseif(CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(WPCAP_LIB_PATH ${SOEM_SOURCE_DIR}/oshw/win32/wpcap/Lib)
endif()

target_link_libraries(soem PUBLIC
  ${WPCAP_LIB_PATH}/wpcap.lib
  ${WPCAP_LIB_PATH}/Packet.lib
  ws2_32.lib
  winmm.lib
)

if(SOEM_INSTALL)
  install(FILES
    osal/win32/osal_defs.h
    oshw/win32/nicdrv.h
    DESTINATION include/soem
  )
endif()
