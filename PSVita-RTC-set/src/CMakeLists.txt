cmake_minimum_required(VERSION 2.8)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(PSVita-RTC-set)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O3 -fno-inline -fno-builtin")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")

add_executable(PSVita-RTC-set
  kernel.c
)

target_link_libraries(PSVita-RTC-set
  SceDebugForDriver_stub
  SceRtcForDriver_stub
  SceModulemgrForDriver_stub
  SceSblACMgrForDriver_stub
  SceSblAIMgrForDriver_stub
  SceSysclibForDriver_stub
  taihenForKernel_stub
  taihenModuleUtils_stub
)

set_target_properties(PSVita-RTC-set
  PROPERTIES LINK_FLAGS "-nostdlib"
)

vita_create_self(rtc_set.skprx PSVita-RTC-set
  CONFIG exports.yml
  UNSAFE
)
