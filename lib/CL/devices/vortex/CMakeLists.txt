add_pocl_device_library(pocl-devices-vortex
  vortex.c
  vortex.h
  vortex_runtime.h
  pocl-vortex-config.h
)

if(VORTEX_DRIVER_INC)
  target_include_directories(pocl-devices-vortex PRIVATE "${VORTEX_DRIVER_INC}")
endif()

if(ENABLE_LOADABLE_DRIVERS AND VORTEX_DRIVER_LIB)
  target_link_libraries(pocl-devices-vortex PRIVATE "${VORTEX_DRIVER_LIB}")
endif()
