#ifndef POCL_VORTEX_H
#define POCL_VORTEX_H

#include "devices.h"
#include "pocl_export.h"

#ifdef __cplusplus
extern "C" {
#endif

POCL_EXPORT void pocl_vortex_init_device_ops(struct pocl_device_ops *ops);
unsigned int pocl_vortex_probe(struct pocl_device_ops *ops);
cl_int pocl_vortex_init(unsigned j, cl_device_id device, const char *parameters);
cl_int pocl_vortex_uninit(unsigned j, cl_device_id device);

#ifdef __cplusplus
}
#endif

#endif
