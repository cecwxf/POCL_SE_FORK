/*
  vortex.c - minimal PoCL device driver for Vortex bring-up

  PR-2 goal: provide a runnable command lifecycle (submit/run/flush/join/notify)
  for buffer kernels while keeping image/USM unsupported.
*/

#include "vortex.h"

#include "common.h"
#include "common_driver.h"
#include "common_utils.h"
#include "devices.h"
#include "pocl_local_size.h"
#include "pocl_util.h"
#include "pocl_workgroup_func.h"
#ifdef ENABLE_LLVM
#include "pocl_llvm.h"
#endif
#include "utlist.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  cl_bool available;
  void *printf_buffer;
  _cl_command_node *ready_list;
  _cl_command_node *command_list;
  pocl_lock_t cq_lock;
} pocl_vortex_data_t;

static const char *vortex_final_ld_flags[] = { "-shared", NULL };

static void vortex_command_scheduler(pocl_vortex_data_t *d)
{
  _cl_command_node *node;

  while ((node = d->ready_list)) {
    assert(pocl_command_is_ready(node->sync.event.event));
    assert(node->sync.event.event->status == CL_SUBMITTED);
    CDL_DELETE(d->ready_list, node);
    POCL_UNLOCK(d->cq_lock);
    pocl_exec_command(node);
    POCL_LOCK(d->cq_lock);
  }
}

static void pocl_vortex_submit(_cl_command_node *node, cl_command_queue cq)
{
  (void)cq;
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)node->device->data;

  if (node != NULL && node->type == CL_COMMAND_NDRANGE_KERNEL) {
    cl_kernel kernel = node->command.run.kernel;
    cl_program program = kernel->program;
    if (!program->builtin_kernel_attributes) {
      void *handle = pocl_check_kernel_dlhandle_cache(node, CL_TRUE, CL_TRUE);
      if (handle == NULL) {
        pocl_update_event_running_unlocked(node->sync.event.event);
        POCL_UNLOCK_OBJ(node->sync.event.event);
        POCL_UPDATE_EVENT_FAILED(CL_FAILED, node->sync.event.event);
        return;
      }
      node->command.run.device_data = handle;
    }
  }

  node->state = POCL_COMMAND_READY;
  POCL_LOCK(d->cq_lock);
  pocl_command_push(node, &d->ready_list, &d->command_list);
  POCL_UNLOCK_OBJ(node->sync.event.event);
  vortex_command_scheduler(d);
  POCL_UNLOCK(d->cq_lock);
}

static void pocl_vortex_flush(cl_device_id device, cl_command_queue cq)
{
  (void)cq;
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)device->data;
  POCL_LOCK(d->cq_lock);
  vortex_command_scheduler(d);
  POCL_UNLOCK(d->cq_lock);
}

static void pocl_vortex_join(cl_device_id device, cl_command_queue cq)
{
  (void)cq;
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)device->data;
  POCL_LOCK(d->cq_lock);
  vortex_command_scheduler(d);
  POCL_UNLOCK(d->cq_lock);
}

static void pocl_vortex_notify(cl_device_id device, cl_event event, cl_event finished)
{
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)device->data;
  _cl_command_node *volatile node = event->command;

  if (finished->status < CL_COMPLETE) {
    pocl_unlock_events_inorder(event, finished);
    pocl_update_event_failed(CL_FAILED, NULL, 0, event, NULL);
    pocl_lock_events_inorder(finished, event);
    return;
  }

  if (node->state != POCL_COMMAND_READY)
    return;

  if (pocl_command_is_ready(event)) {
    if (event->status == CL_QUEUED) {
      pocl_update_event_submitted(event);
      POCL_LOCK(d->cq_lock);
      CDL_DELETE(d->command_list, node);
      CDL_PREPEND(d->ready_list, node);
      POCL_UNLOCK_OBJ(event);
      vortex_command_scheduler(d);
      POCL_LOCK_OBJ(event);
      POCL_UNLOCK(d->cq_lock);
    }
  }
}

static void pocl_vortex_run(void *data, _cl_command_node *cmd)
{
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)data;
  struct pocl_argument *al = NULL;
  size_t x, y, z;
  unsigned i;
  cl_kernel kernel = cmd->command.run.kernel;
  cl_program program = kernel->program;
  pocl_kernel_metadata_t *meta = kernel->meta;
  struct pocl_context *pc = &cmd->command.run.pc;
  cl_uint dev_i = cmd->program_device_i;

  pocl_driver_build_gvar_init_kernel(program, dev_i, cmd->device,
                                     pocl_cpu_gvar_init_callback);

  if (pc->num_groups[0] == 0 || pc->num_groups[1] == 0 || pc->num_groups[2] == 0)
    return;

  void **arguments = (void **)malloc(sizeof(void *) * (meta->num_args + meta->num_locals));
  if (arguments == NULL)
    POCL_ABORT("vortex: failed to allocate kernel argument table\n");

  for (i = 0; i < meta->num_args; ++i) {
    al = &(cmd->command.run.arguments[i]);
    if (ARG_IS_LOCAL(meta->arg_info[i])) {
      arguments[i] = malloc(sizeof(void *));
      *(void **)(arguments[i]) = pocl_aligned_malloc(MAX_EXTENDED_ALIGNMENT, al->size);
    } else if (meta->arg_info[i].type == POCL_ARG_TYPE_POINTER) {
      arguments[i] = malloc(sizeof(void *));
      if (al->value == NULL) {
        *(void **)arguments[i] = NULL;
      } else {
        void *ptr = NULL;
        if (al->is_raw_ptr)
          ptr = *(void **)al->value;
        else {
          cl_mem m = (*(cl_mem *)(al->value));
          ptr = m->device_ptrs[cmd->device->global_mem_id].mem_ptr;
        }
        *(void **)arguments[i] = (char *)ptr;
      }
    } else {
      arguments[i] = al->value;
    }
  }

  for (i = 0; i < meta->num_locals; ++i) {
    size_t s = meta->local_sizes[i];
    size_t j = meta->num_args + i;
    arguments[j] = malloc(sizeof(void *));
    *(void **)(arguments[j]) = pocl_aligned_malloc(MAX_EXTENDED_ALIGNMENT, s);
  }

  pc->printf_buffer = d->printf_buffer;
  uint32_t position = 0;
  pc->printf_buffer_position = &position;
  pc->printf_buffer_capacity = cmd->device->printf_buffer_size;
  pc->global_var_buffer = program->gvar_storage[dev_i];

  unsigned rm, ftz;
  pocl_cpu_save_rm_and_ftz(&rm, &ftz);
  pocl_cpu_setup_rm_and_ftz(cmd->device, program);

  for (z = 0; z < pc->num_groups[2]; ++z)
    for (y = 0; y < pc->num_groups[1]; ++y)
      for (x = 0; x < pc->num_groups[0]; ++x)
        ((pocl_workgroup_func)cmd->command.run.wg)((uint8_t *)arguments,
                                                   (uint8_t *)pc, x, y, z);

  pocl_cpu_restore_rm_and_ftz(rm, ftz);

#ifndef ENABLE_PRINTF_IMMEDIATE_FLUSH
  pocl_write_printf_buffer((char *)d->printf_buffer, position);
#endif

  for (i = 0; i < meta->num_args; ++i) {
    if (ARG_IS_LOCAL(meta->arg_info[i])) {
      POCL_MEM_FREE(*(void **)(arguments[i]));
      POCL_MEM_FREE(arguments[i]);
    } else if (meta->arg_info[i].type == POCL_ARG_TYPE_POINTER) {
      POCL_MEM_FREE(arguments[i]);
    }
  }
  for (i = 0; i < meta->num_locals; ++i) {
    size_t j = meta->num_args + i;
    POCL_MEM_FREE(*(void **)(arguments[j]));
    POCL_MEM_FREE(arguments[j]);
  }
  free(arguments);
}

void pocl_vortex_init_device_ops(struct pocl_device_ops *ops)
{
  ops->device_name = "vortex";

  ops->probe = pocl_vortex_probe;
  ops->init = pocl_vortex_init;
  ops->uninit = pocl_vortex_uninit;

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

  ops->build_source = pocl_driver_build_source;
  ops->link_program = pocl_driver_link_program;
  ops->build_binary = pocl_driver_build_binary;
  ops->free_program = pocl_driver_free_program;
  ops->setup_metadata = pocl_driver_setup_metadata;
  ops->supports_binary = pocl_driver_supports_binary;
  ops->build_poclbinary = pocl_driver_build_poclbinary;
  ops->compile_kernel = NULL;
  ops->build_builtin = pocl_driver_build_opencl_builtins;

  ops->submit = pocl_vortex_submit;
  ops->run = pocl_vortex_run;
  ops->flush = pocl_vortex_flush;
  ops->join = pocl_vortex_join;
  ops->notify = pocl_vortex_notify;
  ops->broadcast = pocl_broadcast;

  ops->build_hash = pocl_cpu_build_hash;
  ops->compute_local_size = pocl_default_local_size_optimizer;

  ops->copy_image_rect = NULL;
  ops->write_image_rect = NULL;
  ops->read_image_rect = NULL;
  ops->map_image = NULL;
  ops->unmap_image = NULL;
  ops->fill_image = NULL;

  ops->svm_alloc = NULL;
  ops->svm_free = NULL;
  ops->usm_alloc = NULL;
  ops->usm_free = NULL;
  ops->usm_free_blocking = NULL;
}

unsigned int pocl_vortex_probe(struct pocl_device_ops *ops)
{
  int env_count = pocl_device_get_env_count(ops->device_name);
  return (env_count < 0) ? 0 : (unsigned)env_count;
}

cl_int pocl_vortex_init(unsigned j, cl_device_id dev, const char *parameters)
{
  (void)j;
  (void)parameters;

  assert(dev->data == NULL);

  pocl_vortex_data_t *data = (pocl_vortex_data_t *)calloc(1, sizeof(*data));
  if (!data)
    return CL_OUT_OF_HOST_MEMORY;

  data->available = CL_TRUE;
  POCL_INIT_LOCK(data->cq_lock);

  pocl_init_default_device_infos(dev, "");
  dev->vendor = "VortexGPGPU";
  dev->vendor_id = 0;
  dev->type = CL_DEVICE_TYPE_GPU;
  dev->long_name = "Vortex Open-Source GPU";
  dev->short_name = "Vortex";
  dev->image_support = CL_FALSE;

  if (dev->max_compute_units == 0)
    dev->max_compute_units = 1;
  if (dev->global_mem_size == 0)
    dev->global_mem_size = 1024UL * 1024UL * 1024UL;
  if (dev->max_mem_alloc_size == 0)
    dev->max_mem_alloc_size = dev->global_mem_size / 2;
  if (dev->local_mem_size == 0)
    dev->local_mem_size = 64UL * 1024UL;
  if (dev->max_constant_buffer_size == 0)
    dev->max_constant_buffer_size = 128UL * 1024UL;
  if (dev->global_mem_cache_size == 0)
    dev->global_mem_cache_size = 256UL * 1024UL;

  SETUP_DEVICE_CL_VERSION(dev, 1, 2);

#ifdef ENABLE_LLVM
  const char *vortex_triple = pocl_get_string_option("POCL_VORTEX_TRIPLE", NULL);
  if (vortex_triple == NULL || vortex_triple[0] == '\0')
    vortex_triple = OCL_KERNEL_TARGET;
#if defined(__x86_64__)
  if (vortex_triple == NULL || vortex_triple[0] == '\0')
    vortex_triple = "x86_64-unknown-linux-gnu";
#elif defined(__aarch64__)
  if (vortex_triple == NULL || vortex_triple[0] == '\0')
    vortex_triple = "aarch64-unknown-linux-gnu";
#elif defined(__riscv) && (__riscv_xlen == 64)
  if (vortex_triple == NULL || vortex_triple[0] == '\0')
    vortex_triple = "riscv64-unknown-linux-gnu";
#endif
  dev->llvm_target_triplet = vortex_triple;
  if (dev->llvm_cpu == NULL || dev->llvm_cpu[0] == '\0')
    dev->llvm_cpu = pocl_get_llvm_cpu_name();

  if (dev->kernellib_subdir == NULL)
    dev->kernellib_subdir = "host";

  if (dev->kernellib_name == NULL || dev->kernellib_fallback_name == NULL)
    {
      char kernellib[POCL_MAX_PATHNAME_LENGTH];
      char kernellib_fallback[POCL_MAX_PATHNAME_LENGTH];
      const char *triple = dev->llvm_target_triplet ? dev->llvm_target_triplet : "";
      const char *cpu = (dev->llvm_cpu && dev->llvm_cpu[0]) ? dev->llvm_cpu : "generic";
      snprintf (kernellib, sizeof (kernellib), "kernel-%s-%s", triple, cpu);
      snprintf (kernellib_fallback, sizeof (kernellib_fallback),
                "kernel-%s-generic", triple);
      if (dev->kernellib_name == NULL)
        dev->kernellib_name = strdup (kernellib);
      if (dev->kernellib_fallback_name == NULL)
        dev->kernellib_fallback_name = strdup (kernellib_fallback);
    }
#endif

  if (dev->final_linkage_flags == NULL)
    dev->final_linkage_flags = vortex_final_ld_flags;

  if (dev->printf_buffer_size == 0)
    dev->printf_buffer_size = 4096;

  data->printf_buffer = pocl_aligned_malloc(MAX_EXTENDED_ALIGNMENT,
                                            dev->printf_buffer_size);
  if (data->printf_buffer == NULL) {
    POCL_DESTROY_LOCK(data->cq_lock);
    free(data);
    return CL_OUT_OF_HOST_MEMORY;
  }

  dev->available = &data->available;
  dev->data = data;
  return CL_SUCCESS;
}

cl_int pocl_vortex_uninit(unsigned j, cl_device_id dev)
{
  (void)j;

  if (dev && dev->data) {
    pocl_vortex_data_t *d = (pocl_vortex_data_t *)dev->data;
    POCL_MEM_FREE(d->printf_buffer);
    POCL_DESTROY_LOCK(d->cq_lock);
    free(d);
    dev->data = NULL;
  }

  return CL_SUCCESS;
}
