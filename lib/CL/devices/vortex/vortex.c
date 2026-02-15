/*
  vortex.c - skeleton PoCL device driver for Vortex GPGPU

  This is an initial bring-up stub to enable building and device enumeration
  in mainline PoCL. Execution / compilation will be implemented in later stages.
*/

#include "vortex.h"

#include "common.h"
#include "common_driver.h"
#include "common_utils.h"
#include "devices.h"
#include "pocl_util.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  cl_bool available;
} pocl_vortex_data_t;

static void pocl_vortex_submit(_cl_command_node *node, cl_command_queue cq)
{
  /* MVP stub: mark as finished immediately.
     Later: enqueue to Vortex command processor and signal events properly. */
  (void)node;
  (void)cq;
  POCL_ABORT_UNIMPLEMENTED("pocl_vortex_submit");
}

void pocl_vortex_init_device_ops(struct pocl_device_ops *ops)
{
  ops->device_name = "vortex";

  ops->probe = pocl_vortex_probe;
  ops->init = pocl_vortex_init;
  ops->uninit = pocl_vortex_uninit;

  /* Memory operations: keep default/common implementations for now. */
  ops->alloc_mem_obj = pocl_driver_alloc_mem_obj;
  ops->free = pocl_driver_free;

  ops->read = pocl_driver_read;
  ops->read_rect = pocl_driver_read_rect;
  ops->write = pocl_driver_write;
  ops->write_rect = pocl_driver_write_rect;
  ops->copy = pocl_driver_copy;
  ops->copy_with_size = pocl_driver_copy_with_size;
  ops->copy_rect = pocl_driver_copy_rect;
  ops->memfill = pocl_driver_memfill;
  ops->map_mem = pocl_driver_map_mem;
  ops->unmap_mem = pocl_driver_unmap_mem;
  ops->get_mapping_ptr = pocl_driver_get_mapping_ptr;
  ops->free_mapping_ptr = pocl_driver_free_mapping_ptr;

  /* Program/kernel build hooks: wire to generic paths for now.
     Later we will add a Vortex-specific LLVM/SPIR-V build pipeline. */
  ops->build_source = pocl_driver_build_source;
  ops->link_program = pocl_driver_link_program;
  ops->build_binary = pocl_driver_build_binary;
  ops->free_program = pocl_driver_free_program;
  ops->setup_metadata = pocl_driver_setup_metadata;
  ops->supports_binary = pocl_driver_supports_binary;
  ops->build_poclbinary = pocl_driver_build_poclbinary;
  ops->compile_kernel = pocl_driver_compile_kernel;
  ops->build_builtin = pocl_driver_build_opencl_builtins;

  /* Command submission/execution: stub for now (MVP-1: enumeration only). */
  ops->submit = pocl_vortex_submit;
  ops->run = NULL;
  ops->flush = pocl_driver_flush;
  ops->join = pocl_driver_join;
  ops->notify = pocl_driver_notify;
  ops->broadcast = pocl_broadcast;

  ops->build_hash = pocl_cpu_build_hash;
  ops->compute_local_size = pocl_default_local_size_optimizer;

  /* Images not supported in MVP. */
  ops->copy_image_rect = NULL;
  ops->write_image_rect = NULL;
  ops->read_image_rect = NULL;
  ops->map_image = NULL;
  ops->unmap_image = NULL;
  ops->fill_image = NULL;
}

unsigned int pocl_vortex_probe(struct pocl_device_ops *ops)
{
  /* Enable via POCL_DEVICES=vortex or POCL_VORTEX=... (env count helper). */
  int env_count = pocl_device_get_env_count(ops->device_name);
  return (env_count < 0) ? 0 : (unsigned)env_count;
}

cl_int pocl_vortex_init(unsigned j, cl_device_id dev, const char *parameters)
{
  (void)j;
  (void)parameters;

  assert(dev->data == NULL);

  pocl_vortex_data_t *data = (pocl_vortex_data_t*)calloc(1, sizeof(*data));
  if (!data)
    return CL_OUT_OF_HOST_MEMORY;
  data->available = CL_TRUE;
  dev->data = data;

  /* Minimal device info. These will be refined to match real Vortex HW. */
  pocl_init_default_device_infos(dev, "");
  dev->vendor = "VortexGPGPU";
  dev->vendor_id = 0;
  dev->type = CL_DEVICE_TYPE_GPU;
  dev->image_support = CL_FALSE;

  /* LLVM target triplet/CPU/kernellib selection TBD during LLVM migration. */
  dev->llvm_target_triplet = "vortex";
  dev->llvm_cpu = "generic";
  dev->kernellib_subdir = "vortex";
  dev->kernellib_fallback_name = NULL;

  SETUP_DEVICE_CL_VERSION(dev, 1, 2);

  return CL_SUCCESS;
}

cl_int pocl_vortex_uninit(unsigned j, cl_device_id dev)
{
  (void)j;
  if (dev && dev->data) {
    free(dev->data);
    dev->data = NULL;
  }
  return CL_SUCCESS;
}
