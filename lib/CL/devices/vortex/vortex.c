/*
  vortex.c - minimal PoCL device driver for Vortex runtime bring-up
*/

#include "vortex.h"

#include "common.h"
#include "common_driver.h"
#include "common_utils.h"
#include "devices.h"
#include "pocl_cache.h"
#include "pocl_local_size.h"
#include "pocl_util.h"
#include "pocl_run_command.h"
#include "pocl_runtime_config.h"
#include "pocl_file_util.h"
#include "utlist.h"

#include "pocl-vortex-config.h"
#include "vortex_runtime.h"

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
  cl_bool available;
  vx_device_h vx_device;
  vx_buffer_h vx_kernel_buffer;
  cl_kernel current_kernel;
  cl_bool is_64bit;
  _cl_command_node *ready_list;
  _cl_command_node *command_list;
  pocl_lock_t cq_lock;
} pocl_vortex_data_t;

typedef struct {
  vx_buffer_h vx_buffer;
} pocl_vortex_mem_t;

static inline void
vortex_store_ptr (uint8_t *dst, size_t ptr_size, uint64_t value)
{
  if (ptr_size == 8)
    {
      uint64_t tmp = value;
      memcpy (dst, &tmp, 8);
    }
  else
    {
      uint32_t tmp = (uint32_t)value;
      memcpy (dst, &tmp, 4);
    }
}

static char *
pocl_vortex_build_hash (cl_device_id device)
{
  const char *triple = (device->llvm_target_triplet != NULL)
                           ? device->llvm_target_triplet
                           : "unknown";
  const char *cflags = pocl_get_string_option ("POCL_VORTEX_CFLAGS", "");
  size_t len = strlen (triple) + strlen (cflags) + 32;
  char *res = (char *)calloc (len, 1);
  if (res != NULL)
    snprintf (res, len, "vortex-%s-%s", triple, cflags);
  return res;
}

static void
vortex_command_scheduler (pocl_vortex_data_t *d)
{
  _cl_command_node *node;

  while ((node = d->ready_list))
    {
      assert (pocl_command_is_ready (node->sync.event.event));
      assert (node->sync.event.event->status == CL_SUBMITTED);
      CDL_DELETE (d->ready_list, node);
      POCL_UNLOCK (d->cq_lock);
      pocl_exec_command (node);
      POCL_LOCK (d->cq_lock);
    }
}

static int
pocl_vortex_run_shell_cmd_capture (const char *cmd, char *capture,
                                   size_t capture_capacity)
{
  const char *args[] = { "/bin/sh", "-c", cmd, NULL };

  if (capture != NULL && capture_capacity > 0)
    capture[0] = 0;

  size_t captured = (capture != NULL && capture_capacity > 0)
                        ? (capture_capacity - 1)
                        : 0;

  POCL_MSG_PRINT_LLVM ("vortex finalize: %s\n", cmd);

  int ret = 0;
  if (capture != NULL && capture_capacity > 0)
    ret = pocl_run_command_capture_output (capture, &captured, args);
  else
    {
      char sink[2] = { 0 };
      size_t sink_size = 1;
      ret = pocl_run_command_capture_output (sink, &sink_size, args);
    }

  if (capture != NULL && capture_capacity > 0)
    {
      if (captured < capture_capacity)
        capture[captured] = 0;
      else
        capture[capture_capacity - 1] = 0;

      if (capture[0] != 0)
        POCL_MSG_PRINT_LLVM ("%s\n", capture);
    }

  if (ret != 0)
    POCL_MSG_ERR ("vortex finalize command failed (%d): %s\n", ret, cmd);

  return ret;
}

static int
pocl_vortex_run_shell_cmd (const char *cmd)
{
  char capture[8192];
  return pocl_vortex_run_shell_cmd_capture (cmd, capture, sizeof (capture));
}

static int
pocl_vortex_is_workgroup_entry_symbol (const char *symbol)
{
  static const char Prefix[] = "_pocl_kernel_";
  static const char Suffix[] = "_workgroup";

  if (symbol == NULL)
    return 0;

  size_t sym_len = strlen (symbol);
  size_t prefix_len = sizeof (Prefix) - 1;
  size_t suffix_len = sizeof (Suffix) - 1;

  if (sym_len <= prefix_len + suffix_len)
    return 0;

  return (strncmp (symbol, Prefix, prefix_len) == 0
          && strcmp (symbol + sym_len - suffix_len, Suffix) == 0);
}

static int
pocl_vortex_resolve_entry_symbol (const char *input_binary, char *entry_symbol,
                                  size_t entry_symbol_size)
{
  const char *nm_tool = pocl_get_string_option ("POCL_VORTEX_NM", "llvm-nm");

  if (entry_symbol == NULL || entry_symbol_size == 0)
    return -1;

  entry_symbol[0] = 0;

  char nm_cmd[4096];
  int n = snprintf (nm_cmd, sizeof (nm_cmd),
                    "%s --defined-only --just-symbol-name '%s'", nm_tool,
                    input_binary);
  if (n < 0 || (size_t)n >= sizeof (nm_cmd))
    {
      POCL_MSG_ERR ("vortex finalize: nm command too long\n");
      return -1;
    }

  char nm_output[16384];
  if (pocl_vortex_run_shell_cmd_capture (nm_cmd, nm_output, sizeof (nm_output))
      != 0)
    {
      POCL_MSG_ERR ("vortex finalize: failed to run llvm-nm for %s\n",
                    input_binary);
      return -1;
    }

  char *saveptr = NULL;
  for (char *line = strtok_r (nm_output, "\n", &saveptr); line != NULL;
       line = strtok_r (NULL, "\n", &saveptr))
    {
      while (isspace ((unsigned char)*line))
        ++line;

      size_t len = strlen (line);
      while (len > 0 && isspace ((unsigned char)line[len - 1]))
        line[--len] = 0;

      if (len == 0)
        continue;

      if (pocl_vortex_is_workgroup_entry_symbol (line))
        {
          size_t copy_len = len;
          if (copy_len >= entry_symbol_size)
            copy_len = entry_symbol_size - 1;
          memcpy (entry_symbol, line, copy_len);
          entry_symbol[copy_len] = 0;
          POCL_MSG_PRINT_LLVM (
              "vortex finalize: resolved kernel entry symbol: %s\n",
              entry_symbol);
          return 0;
        }
    }

  POCL_MSG_ERR (
      "vortex finalize: no _pocl_kernel_*_workgroup symbol found in %s\n",
      input_binary);
  return -1;
}

static int
pocl_vortex_validate_elf_load_segments (const char *elf_path)
{
  const char *readelf_tool
      = pocl_get_string_option ("POCL_VORTEX_READELF", "llvm-readelf");

  char readelf_cmd[4096];
  int n = snprintf (readelf_cmd, sizeof (readelf_cmd), "%s -l '%s'",
                    readelf_tool, elf_path);
  if (n < 0 || (size_t)n >= sizeof (readelf_cmd))
    {
      POCL_MSG_ERR ("vortex finalize: readelf command too long\n");
      return -1;
    }

  char readelf_output[16384];
  if (pocl_vortex_run_shell_cmd_capture (readelf_cmd, readelf_output,
                                         sizeof (readelf_output))
      != 0)
    {
      POCL_MSG_ERR ("vortex finalize: readelf failed for %s\n", elf_path);
      return -1;
    }

  unsigned load_segments = 0;
  char *saveptr = NULL;
  for (char *line = strtok_r (readelf_output, "\n", &saveptr); line != NULL;
       line = strtok_r (NULL, "\n", &saveptr))
    {
      while (isspace ((unsigned char)*line))
        ++line;

      if (strncmp (line, "LOAD", 4) == 0
          && (line[4] == ' ' || line[4] == '\t' || line[4] == 0))
        ++load_segments;
    }

  if (load_segments == 0)
    {
      POCL_MSG_ERR ("vortex finalize: ELF %s has no LOAD segment\n", elf_path);
      return -1;
    }

  POCL_MSG_PRINT_LLVM ("vortex finalize: ELF %s has %u LOAD segment(s)\n",
                       elf_path, load_segments);
  return 0;
}

static int
pocl_vortex_validate_vxbin_size (const char *output_binary)
{
  struct stat st;
  if (stat (output_binary, &st) != 0)
    {
      POCL_MSG_ERR ("vortex finalize: stat(%s) failed: %s\n", output_binary,
                    strerror (errno));
      return -1;
    }

  if (!S_ISREG (st.st_mode))
    {
      POCL_MSG_ERR ("vortex finalize: %s is not a regular file\n",
                    output_binary);
      return -1;
    }

  int min_size = pocl_get_int_option ("POCL_VORTEX_MIN_VXBIN_SIZE", 256);
  if (min_size < 1)
    min_size = 1;

  if (st.st_size < (off_t)min_size)
    {
      POCL_MSG_ERR ("vortex finalize: vxbin too small: %lld bytes (< %d) at %s\n",
                    (long long)st.st_size, min_size, output_binary);
      return -1;
    }

  POCL_MSG_PRINT_LLVM ("vortex finalize: vxbin size=%lld bytes (threshold=%d)\n",
                       (long long)st.st_size, min_size);
  return 0;
}

static int
pocl_vortex_finalize_binary (cl_device_id device, const char *output_binary,
                             const char *input_binary)
{
  (void)device;

  const char *clang = pocl_get_path ("CLANG", "clang");
  const char *vortex_cflags
      = pocl_get_string_option ("POCL_VORTEX_CFLAGS", "");
  const char *vortex_finalize_cflags
      = pocl_get_string_option ("POCL_VORTEX_FINALIZE_CFLAGS", "");
  if (vortex_finalize_cflags == NULL || vortex_finalize_cflags[0] == 0)
    vortex_finalize_cflags = vortex_cflags;
  const char *vortex_ldflags
      = pocl_get_string_option ("POCL_VORTEX_LDFLAGS", "");
  const char *vortex_bintool
      = pocl_get_string_option ("POCL_VORTEX_BINTOOL", "");
  const char *vortex_objcopy
      = pocl_get_string_option ("POCL_VORTEX_OBJCOPY", "llvm-objcopy");
  const char *vortex_wrapper_cflags
      = pocl_get_string_option (
          "POCL_VORTEX_WRAPPER_CFLAGS",
          "--target=riscv32-unknown-elf -march=rv32im -mabi=ilp32 -nostdlib");
  const char *vortex_stack_offset
      = pocl_get_string_option ("POCL_VORTEX_STACK_OFFSET", "65536");

  if (vortex_bintool == NULL || vortex_bintool[0] == 0)
    {
      POCL_MSG_ERR ("POCL_VORTEX_BINTOOL is not set\n");
      return -1;
    }

  char entry_symbol[512];
  if (pocl_vortex_resolve_entry_symbol (input_binary, entry_symbol,
                                        sizeof (entry_symbol))
      != 0)
    {
      return -1;
    }

  char entry_impl_symbol[640];
  int n = snprintf (entry_impl_symbol, sizeof (entry_impl_symbol),
                    "%s__pocl_impl", entry_symbol);
  if (n < 0 || (size_t)n >= sizeof (entry_impl_symbol))
    {
      POCL_MSG_ERR ("vortex finalize: entry symbol rename is too long\n");
      return -1;
    }

  char temp_elf[POCL_MAX_PATHNAME_LENGTH];
  char temp_obj[POCL_MAX_PATHNAME_LENGTH];
  char wrapper_src[POCL_MAX_PATHNAME_LENGTH];
  char wrapper_obj[POCL_MAX_PATHNAME_LENGTH];

  if (pocl_mk_tempname (temp_elf, output_binary, ".elf", NULL) != 0
      || pocl_mk_tempname (temp_obj, output_binary, ".entry.o", NULL) != 0
      || pocl_mk_tempname (wrapper_src, output_binary, ".entry.S", NULL) != 0
      || pocl_mk_tempname (wrapper_obj, output_binary, ".entrywrap.o", NULL)
             != 0)
    {
      POCL_MSG_ERR ("vortex: unable to create temporary finalize files\n");
      return -1;
    }

  int ret = -1;

  char wrapper_code[4096];
  n = snprintf (
      wrapper_code, sizeof (wrapper_code),
      ".text\n"
      ".globl %s\n"
      ".type %s, @function\n"
      "%s:\n"
      "  li t0, 1\n"
      "  .insn r 0x0b, 0, 0, x0, t0, x0\n"
      "  .option push\n"
      "  .option norelax\n"
      "  la gp, __global_pointer\n"
      "  .option pop\n"
      "  li t1, %s\n"
      "  add sp, a0, t1\n"
      "  mv t0, a0\n"
      "  addi a0, t0, %u\n"
      "  mv a1, t0\n"
      "  li a2, 0\n"
      "  li a3, 0\n"
      "  li a4, 0\n"
      "  call %s\n"
      "  li t0, 0x88\n"
      "  sw zero, 0(t0)\n"
      "  fence\n"
      "  .insn r 0x0b, 0, 0, x0, x0, x0\n"
      "1:\n"
      "  j 1b\n"
      ".size %s, .-%s\n",
      entry_symbol, entry_symbol, entry_symbol, vortex_stack_offset,
      (unsigned)ALIGNED_CTX_SIZE, entry_impl_symbol, entry_symbol,
      entry_symbol);
  if (n < 0 || (size_t)n >= sizeof (wrapper_code))
    {
      POCL_MSG_ERR ("vortex finalize: wrapper source overflow\n");
      goto FINISH;
    }

  if (pocl_write_file (wrapper_src, wrapper_code, strlen (wrapper_code), 0)
      != 0)
    {
      POCL_MSG_ERR ("vortex finalize: failed to write wrapper source %s\n",
                    wrapper_src);
      goto FINISH;
    }

  char rename_cmd[8192];
  n = snprintf (rename_cmd, sizeof (rename_cmd),
                "%s --redefine-sym %s=%s '%s' '%s'", vortex_objcopy,
                entry_symbol, entry_impl_symbol, input_binary, temp_obj);
  if (n < 0 || (size_t)n >= sizeof (rename_cmd))
    {
      POCL_MSG_ERR ("vortex finalize: objcopy command too long\n");
      goto FINISH;
    }

  if (pocl_vortex_run_shell_cmd (rename_cmd) != 0)
    goto FINISH;

  char compile_cmd[8192];
  n = snprintf (compile_cmd, sizeof (compile_cmd),
                "%s %s -c '%s' -o '%s'", clang, vortex_wrapper_cflags,
                wrapper_src, wrapper_obj);
  if (n < 0 || (size_t)n >= sizeof (compile_cmd))
    {
      POCL_MSG_ERR ("vortex finalize: wrapper compile command too long\n");
      goto FINISH;
    }

  if (pocl_vortex_run_shell_cmd (compile_cmd) != 0)
    goto FINISH;

  char link_cmd[8192];
  n = snprintf (link_cmd, sizeof (link_cmd),
                "%s %s '%s' '%s' %s -Wl,-e,%s -o '%s'", clang,
                vortex_finalize_cflags, temp_obj, wrapper_obj, vortex_ldflags,
                entry_symbol, temp_elf);
  if (n < 0 || (size_t)n >= sizeof (link_cmd))
    {
      POCL_MSG_ERR ("vortex: link command too long\n");
      goto FINISH;
    }

  if (pocl_vortex_run_shell_cmd (link_cmd) != 0)
    goto FINISH;

  if (pocl_vortex_validate_elf_load_segments (temp_elf) != 0)
    goto FINISH;

  char pack_cmd[8192];
  n = snprintf (pack_cmd, sizeof (pack_cmd), "%s '%s' '%s'", vortex_bintool,
                temp_elf, output_binary);
  if (n < 0 || (size_t)n >= sizeof (pack_cmd))
    {
      POCL_MSG_ERR ("vortex: bintool command too long\n");
      goto FINISH;
    }

  if (pocl_vortex_run_shell_cmd (pack_cmd) != 0)
    goto FINISH;

  if (pocl_vortex_validate_vxbin_size (output_binary) != 0)
    goto FINISH;

  ret = 0;

FINISH:
  if (!pocl_get_bool_option ("POCL_LEAVE_KERNEL_COMPILER_TEMP_FILES", 0))
    {
      pocl_remove (temp_elf);
      pocl_remove (temp_obj);
      pocl_remove (wrapper_src);
      pocl_remove (wrapper_obj);
    }

  return ret;
}

static int
pocl_vortex_compile_kernel (_cl_command_node *cmd, cl_kernel kernel,
                            cl_device_id device, int specialize)
{
  (void)kernel;
  (void)device;

  if (cmd == NULL || cmd->type != CL_COMMAND_NDRANGE_KERNEL)
    return CL_SUCCESS;

  char module_path[POCL_MAX_PATHNAME_LENGTH];
  return pocl_check_kernel_disk_cache (module_path, cmd, specialize);
}

static void
pocl_vortex_submit (_cl_command_node *node, cl_command_queue cq)
{
  (void)cq;
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)node->device->data;

  node->state = POCL_COMMAND_READY;
  POCL_LOCK (d->cq_lock);
  pocl_command_push (node, &d->ready_list, &d->command_list);
  POCL_UNLOCK_OBJ (node->sync.event.event);
  vortex_command_scheduler (d);
  POCL_UNLOCK (d->cq_lock);
}

static void
pocl_vortex_flush (cl_device_id device, cl_command_queue cq)
{
  (void)cq;
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)device->data;
  POCL_LOCK (d->cq_lock);
  vortex_command_scheduler (d);
  POCL_UNLOCK (d->cq_lock);
}

static void
pocl_vortex_join (cl_device_id device, cl_command_queue cq)
{
  (void)cq;
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)device->data;
  POCL_LOCK (d->cq_lock);
  vortex_command_scheduler (d);
  POCL_UNLOCK (d->cq_lock);
}

static void
pocl_vortex_notify (cl_device_id device, cl_event event, cl_event finished)
{
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)device->data;
  _cl_command_node *volatile node = event->command;

  if (finished->status < CL_COMPLETE)
    {
      pocl_unlock_events_inorder (event, finished);
      pocl_update_event_failed (CL_FAILED, NULL, 0, event, NULL);
      pocl_lock_events_inorder (finished, event);
      return;
    }

  if (node->state != POCL_COMMAND_READY)
    return;

  if (pocl_command_is_ready (event))
    {
      if (event->status == CL_QUEUED)
        {
          pocl_update_event_submitted (event);
          POCL_LOCK (d->cq_lock);
          CDL_DELETE (d->command_list, node);
          CDL_PREPEND (d->ready_list, node);
          POCL_UNLOCK_OBJ (event);
          vortex_command_scheduler (d);
          POCL_LOCK_OBJ (event);
          POCL_UNLOCK (d->cq_lock);
        }
    }
}

static cl_int
pocl_vortex_alloc_mem_obj (cl_device_id device, cl_mem mem_obj, void *host_ptr)
{
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)device->data;
  pocl_mem_identifier *p = &mem_obj->device_ptrs[device->global_mem_id];

  p->mem_ptr = NULL;
  p->extra_ptr = NULL;
  p->version = 0;
  p->extra = 0;

  pocl_vortex_mem_t *buf = (pocl_vortex_mem_t *)calloc (1, sizeof (*buf));
  if (buf == NULL)
    return CL_OUT_OF_HOST_MEMORY;

  int flags = VX_MEM_READ_WRITE;
  if (mem_obj->flags & CL_MEM_READ_ONLY)
    flags = VX_MEM_READ;
  if (mem_obj->flags & CL_MEM_WRITE_ONLY)
    flags = VX_MEM_WRITE;

  int vx_err = vx_mem_alloc (d->vx_device, mem_obj->size, flags, &buf->vx_buffer);
  if (vx_err != 0)
    {
      free (buf);
      return CL_MEM_OBJECT_ALLOCATION_FAILURE;
    }

  if (host_ptr != NULL && (mem_obj->flags & CL_MEM_COPY_HOST_PTR))
    {
      vx_err = vx_copy_to_dev (buf->vx_buffer, host_ptr, 0, mem_obj->size);
      if (vx_err != 0)
        {
          vx_mem_free (buf->vx_buffer);
          free (buf);
          return CL_MEM_OBJECT_ALLOCATION_FAILURE;
        }
    }

  p->mem_ptr = buf;
  return CL_SUCCESS;
}

static void
pocl_vortex_free (cl_device_id device, cl_mem mem_obj)
{
  pocl_mem_identifier *p = &mem_obj->device_ptrs[device->global_mem_id];
  pocl_vortex_mem_t *buf = (pocl_vortex_mem_t *)p->mem_ptr;
  if (buf != NULL)
    {
      if (buf->vx_buffer != NULL)
        vx_mem_free (buf->vx_buffer);
      free (buf);
    }
  p->mem_ptr = NULL;
  p->version = 0;
}

static void
pocl_vortex_write (void *data, const void *__restrict__ host_ptr,
                   pocl_mem_identifier *dst_mem_id, cl_mem dst_buf,
                   size_t offset, size_t size)
{
  (void)data;
  (void)dst_buf;
  pocl_vortex_mem_t *buf = (pocl_vortex_mem_t *)dst_mem_id->mem_ptr;
  if (buf == NULL || vx_copy_to_dev (buf->vx_buffer, host_ptr, offset, size) != 0)
    POCL_ABORT ("vortex: vx_copy_to_dev failed\n");
}

static void
pocl_vortex_read (void *data, void *__restrict__ host_ptr,
                  pocl_mem_identifier *src_mem_id, cl_mem src_buf, size_t offset,
                  size_t size)
{
  (void)data;
  (void)src_buf;
  pocl_vortex_mem_t *buf = (pocl_vortex_mem_t *)src_mem_id->mem_ptr;
  if (buf == NULL || vx_copy_from_dev (host_ptr, buf->vx_buffer, offset, size) != 0)
    POCL_ABORT ("vortex: vx_copy_from_dev failed\n");
}

static void
pocl_vortex_copy (void *data, pocl_mem_identifier *dst_mem_id, cl_mem dst_buf,
                  pocl_mem_identifier *src_mem_id, cl_mem src_buf,
                  size_t dst_offset, size_t src_offset, size_t size)
{
  (void)dst_buf;
  (void)src_buf;
  char *tmp = (char *)malloc (size);
  if (tmp == NULL)
    POCL_ABORT ("vortex: malloc failed in copy\n");
  pocl_vortex_read (data, tmp, src_mem_id, src_buf, src_offset, size);
  pocl_vortex_write (data, tmp, dst_mem_id, dst_buf, dst_offset, size);
  free (tmp);
}

static void
pocl_vortex_copy_with_size (void *data, pocl_mem_identifier *dst_mem_id,
                            cl_mem dst_buf, pocl_mem_identifier *src_mem_id,
                            cl_mem src_buf,
                            pocl_mem_identifier *content_size_buf_mem_id,
                            cl_mem content_size_buf, size_t dst_offset,
                            size_t src_offset, size_t size)
{
  size_t content_size = size;
  if (content_size_buf_mem_id != NULL)
    {
      pocl_vortex_read (data, &content_size, content_size_buf_mem_id,
                        content_size_buf, 0, sizeof (content_size));
      if (content_size > size)
        content_size = size;
    }
  pocl_vortex_copy (data, dst_mem_id, dst_buf, src_mem_id, src_buf, dst_offset,
                    src_offset, content_size);
}

static void
pocl_vortex_memfill (void *data, pocl_mem_identifier *dst_mem_id, cl_mem dst_buf,
                     size_t size, size_t offset,
                     const void *__restrict__ pattern, size_t pattern_size)
{
  (void)dst_buf;
  char *tmp = (char *)malloc (size);
  if (tmp == NULL)
    POCL_ABORT ("vortex: malloc failed in memfill\n");
  for (size_t i = 0; i < size; ++i)
    tmp[i] = ((const char *)pattern)[i % pattern_size];
  pocl_vortex_write (data, tmp, dst_mem_id, dst_buf, offset, size);
  free (tmp);
}

static cl_int
pocl_vortex_map_mem (void *data, pocl_mem_identifier *src_mem_id, cl_mem src_buf,
                     mem_mapping_t *map)
{
  if (map->map_flags & CL_MAP_WRITE_INVALIDATE_REGION)
    return CL_SUCCESS;

  pocl_vortex_read (data, map->host_ptr, src_mem_id, src_buf, map->offset,
                    map->size);
  return CL_SUCCESS;
}

static cl_int
pocl_vortex_unmap_mem (void *data, pocl_mem_identifier *dst_mem_id, cl_mem dst_buf,
                       mem_mapping_t *map)
{
  if (map->map_flags == CL_MAP_READ)
    return CL_SUCCESS;

  pocl_vortex_write (data, map->host_ptr, dst_mem_id, dst_buf, map->offset,
                     map->size);
  return CL_SUCCESS;
}

static void
pocl_vortex_write_rect (void *data, const void *__restrict__ src_host_ptr,
                        pocl_mem_identifier *dst_mem_id, cl_mem dst_buf,
                        const size_t *buffer_origin, const size_t *host_origin,
                        const size_t *region, size_t buffer_row_pitch,
                        size_t buffer_slice_pitch, size_t host_row_pitch,
                        size_t host_slice_pitch)
{
  (void)data;
  (void)src_host_ptr;
  (void)dst_mem_id;
  (void)dst_buf;
  (void)buffer_origin;
  (void)host_origin;
  (void)region;
  (void)buffer_row_pitch;
  (void)buffer_slice_pitch;
  (void)host_row_pitch;
  (void)host_slice_pitch;
  POCL_ABORT ("vortex: write_rect not implemented\n");
}

static void
pocl_vortex_read_rect (void *data, void *__restrict__ dst_host_ptr,
                       pocl_mem_identifier *src_mem_id, cl_mem src_buf,
                       const size_t *buffer_origin, const size_t *host_origin,
                       const size_t *region, size_t buffer_row_pitch,
                       size_t buffer_slice_pitch, size_t host_row_pitch,
                       size_t host_slice_pitch)
{
  (void)data;
  (void)dst_host_ptr;
  (void)src_mem_id;
  (void)src_buf;
  (void)buffer_origin;
  (void)host_origin;
  (void)region;
  (void)buffer_row_pitch;
  (void)buffer_slice_pitch;
  (void)host_row_pitch;
  (void)host_slice_pitch;
  POCL_ABORT ("vortex: read_rect not implemented\n");
}

static void
pocl_vortex_copy_rect (void *data, pocl_mem_identifier *dst_mem_id, cl_mem dst_buf,
                       pocl_mem_identifier *src_mem_id, cl_mem src_buf,
                       const size_t *dst_origin, const size_t *src_origin,
                       const size_t *region, size_t dst_row_pitch,
                       size_t dst_slice_pitch, size_t src_row_pitch,
                       size_t src_slice_pitch)
{
  (void)data;
  (void)dst_mem_id;
  (void)dst_buf;
  (void)src_mem_id;
  (void)src_buf;
  (void)dst_origin;
  (void)src_origin;
  (void)region;
  (void)dst_row_pitch;
  (void)dst_slice_pitch;
  (void)src_row_pitch;
  (void)src_slice_pitch;
  POCL_ABORT ("vortex: copy_rect not implemented\n");
}

static void
pocl_vortex_run (void *data, _cl_command_node *cmd)
{
  pocl_vortex_data_t *d = (pocl_vortex_data_t *)data;
  cl_kernel kernel = cmd->command.run.kernel;
  cl_program program = kernel->program;
  pocl_kernel_metadata_t *meta = kernel->meta;
  struct pocl_context *pc = &cmd->command.run.pc;
  cl_uint dev_i = cmd->program_device_i;

  size_t num_groups = pc->num_groups[0] * pc->num_groups[1] * pc->num_groups[2];
  if (num_groups == 0)
    return;

  const size_t ptr_size = d->is_64bit ? 8 : 4;
  size_t abuf_args_size = ptr_size * (meta->num_args + meta->num_locals);
  size_t abuf_size = ALIGNED_CTX_SIZE + abuf_args_size;
  size_t local_mem_size = 0;

  for (unsigned i = 0; i < meta->num_args; ++i)
    {
      struct pocl_argument *al = &cmd->command.run.arguments[i];
      if (ARG_IS_LOCAL (meta->arg_info[i]))
        {
          local_mem_size += al->size;
          abuf_size += ptr_size;
        }
      else if (meta->arg_info[i].type == POCL_ARG_TYPE_POINTER)
        {
          abuf_size += ptr_size;
        }
      else if (meta->arg_info[i].type == POCL_ARG_TYPE_IMAGE
               || meta->arg_info[i].type == POCL_ARG_TYPE_SAMPLER)
        {
          POCL_ABORT ("vortex: images/samplers are not supported\n");
        }
      else
        {
          abuf_size += al->size;
        }
    }

  for (unsigned i = 0; i < meta->num_locals; ++i)
    {
      local_mem_size += meta->local_sizes[i];
      abuf_size += ptr_size;
    }

  size_t stack_slack = (size_t)pocl_get_int_option ("POCL_VORTEX_STACK_SLACK", 69632);
  if (stack_slack < 4096)
    stack_slack = 4096;
  abuf_size += stack_slack;

  uint8_t *host_args_base_ptr = (uint8_t *)calloc (1, abuf_size);
  if (host_args_base_ptr == NULL)
    POCL_ABORT ("vortex: host args buffer allocation failed\n");

  vx_buffer_h vx_args_buffer = NULL;
  if (vx_mem_alloc (d->vx_device, abuf_size, VX_MEM_READ, &vx_args_buffer) != 0)
    POCL_ABORT ("vortex: vx_mem_alloc(args) failed\n");

  uint64_t dev_args_base_addr = 0;
  if (vx_mem_address (vx_args_buffer, &dev_args_base_addr) != 0)
    POCL_ABORT ("vortex: vx_mem_address(args) failed\n");

  vx_buffer_h vx_local_buffer = NULL;
  uint64_t local_mem_addr = 0;
  if (local_mem_size > 0)
    {
      if (vx_mem_alloc (d->vx_device, local_mem_size, VX_MEM_READ_WRITE,
                        &vx_local_buffer)
          != 0)
        POCL_ABORT ("vortex: vx_mem_alloc(local) failed\n");
      if (vx_mem_address (vx_local_buffer, &local_mem_addr) != 0)
        POCL_ABORT ("vortex: vx_mem_address(local) failed\n");
    }

  {
    pocl_kernel_context_t *ctx = (pocl_kernel_context_t *)host_args_base_ptr;
    for (int i = 0; i < 3; ++i)
      {
        ctx->num_groups[i] = pc->num_groups[i];
        ctx->global_offset[i] = pc->global_offset[i];
        ctx->local_size[i] = pc->local_size[i];
      }
    ctx->work_dim = pc->work_dim;
    ctx->printf_buffer = 0;
    ctx->printf_buffer_position = 0;
    ctx->printf_buffer_capacity = 0;
  }

  uint8_t *host_args_ptr = host_args_base_ptr + ALIGNED_CTX_SIZE;
  uint64_t dev_data_addr = dev_args_base_addr + ALIGNED_CTX_SIZE + abuf_args_size;

  for (unsigned i = 0; i < meta->num_args; ++i)
    {
      struct pocl_argument *al = &cmd->command.run.arguments[i];
      vortex_store_ptr (host_args_ptr, ptr_size, dev_data_addr);

      uint8_t *slot = host_args_base_ptr + (dev_data_addr - dev_args_base_addr);

      if (ARG_IS_LOCAL (meta->arg_info[i]))
        {
          vortex_store_ptr (slot, ptr_size, local_mem_addr);
          local_mem_addr += al->size;
          dev_data_addr += ptr_size;
        }
      else if (meta->arg_info[i].type == POCL_ARG_TYPE_POINTER)
        {
          uint64_t dev_ptr = 0;
          if (al->value != NULL)
            {
              cl_mem m = (*(cl_mem *)(al->value));
              pocl_vortex_mem_t *buf =
                  (pocl_vortex_mem_t *)m->device_ptrs[cmd->device->global_mem_id]
                      .mem_ptr;
              if (buf != NULL && vx_mem_address (buf->vx_buffer, &dev_ptr) == 0)
                dev_ptr += al->offset;
            }
          vortex_store_ptr (slot, ptr_size, dev_ptr);
          dev_data_addr += ptr_size;
        }
      else
        {
          memcpy (slot, al->value, al->size);
          dev_data_addr += al->size;
        }

      host_args_ptr += ptr_size;
    }

  for (unsigned i = 0; i < meta->num_locals; ++i)
    {
      vortex_store_ptr (host_args_ptr, ptr_size, dev_data_addr);
      uint8_t *slot = host_args_base_ptr + (dev_data_addr - dev_args_base_addr);
      vortex_store_ptr (slot, ptr_size, local_mem_addr);
      local_mem_addr += meta->local_sizes[i];
      host_args_ptr += ptr_size;
      dev_data_addr += ptr_size;
    }

  if (vx_copy_to_dev (vx_args_buffer, host_args_base_ptr, 0, abuf_size) != 0)
    POCL_ABORT ("vortex: vx_copy_to_dev(args) failed\n");

  free (host_args_base_ptr);

  if (d->current_kernel != kernel)
    {
      d->current_kernel = kernel;
      if (d->vx_kernel_buffer != NULL)
        {
          vx_mem_free (d->vx_kernel_buffer);
          d->vx_kernel_buffer = NULL;
        }

      char program_bin_path[POCL_MAX_FILENAME_LENGTH];
      if (pocl_check_kernel_disk_cache (program_bin_path, cmd, 0) != CL_SUCCESS)
        POCL_ABORT ("vortex: failed to get kernel binary path\n");

      if (vx_upload_kernel_file (d->vx_device, program_bin_path,
                                 &d->vx_kernel_buffer)
          != 0)
        POCL_ABORT ("vortex: vx_upload_kernel_file failed for %s\n",
                    program_bin_path);
    }

  if (vx_start (d->vx_device, d->vx_kernel_buffer, vx_args_buffer) != 0)
    POCL_ABORT ("vortex: vx_start failed\n");

  if (vx_ready_wait (d->vx_device, VX_MAX_TIMEOUT) != 0)
    POCL_ABORT ("vortex: vx_ready_wait failed\n");

  if (vx_local_buffer != NULL)
    vx_mem_free (vx_local_buffer);
  vx_mem_free (vx_args_buffer);
}

void
pocl_vortex_init_device_ops (struct pocl_device_ops *ops)
{
  ops->device_name = "vortex";

  ops->probe = pocl_vortex_probe;
  ops->init = pocl_vortex_init;
  ops->uninit = pocl_vortex_uninit;

  ops->alloc_mem_obj = pocl_vortex_alloc_mem_obj;
  ops->free = pocl_vortex_free;

  ops->read = pocl_vortex_read;
  ops->read_rect = pocl_vortex_read_rect;
  ops->write = pocl_vortex_write;
  ops->write_rect = pocl_vortex_write_rect;
  ops->copy = pocl_vortex_copy;
  ops->copy_with_size = pocl_vortex_copy_with_size;
  ops->copy_rect = pocl_vortex_copy_rect;
  ops->memfill = pocl_vortex_memfill;
  ops->map_mem = pocl_vortex_map_mem;
  ops->unmap_mem = pocl_vortex_unmap_mem;
  ops->get_mapping_ptr = pocl_driver_get_mapping_ptr;
  ops->free_mapping_ptr = pocl_driver_free_mapping_ptr;

  ops->build_source = pocl_driver_build_source;
  ops->link_program = pocl_driver_link_program;
  ops->build_binary = pocl_driver_build_binary;
  ops->free_program = pocl_driver_free_program;
  ops->setup_metadata = pocl_driver_setup_metadata;
  ops->supports_binary = pocl_driver_supports_binary;
  ops->build_poclbinary = pocl_driver_build_poclbinary;
  ops->compile_kernel = pocl_vortex_compile_kernel;
  ops->finalize_binary = pocl_vortex_finalize_binary;
  ops->build_builtin = pocl_driver_build_opencl_builtins;

  ops->submit = pocl_vortex_submit;
  ops->run = pocl_vortex_run;
  ops->flush = pocl_vortex_flush;
  ops->join = pocl_vortex_join;
  ops->notify = pocl_vortex_notify;
  ops->broadcast = pocl_broadcast;

  ops->build_hash = pocl_vortex_build_hash;
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

unsigned int
pocl_vortex_probe (struct pocl_device_ops *ops)
{
  int env_count = pocl_device_get_env_count (ops->device_name);
  return (env_count < 0) ? 0 : (unsigned)env_count;
}

cl_int
pocl_vortex_init (unsigned j, cl_device_id dev, const char *parameters)
{
  (void)j;
  (void)parameters;

  assert (dev->data == NULL);

  pocl_vortex_data_t *data = (pocl_vortex_data_t *)calloc (1, sizeof (*data));
  if (!data)
    return CL_OUT_OF_HOST_MEMORY;

  int vx_err = vx_dev_open (&data->vx_device);
  if (vx_err != 0)
    {
      free (data);
      return CL_DEVICE_NOT_FOUND;
    }

  data->available = CL_TRUE;
  POCL_INIT_LOCK (data->cq_lock);

  uint64_t num_cores = 1;
  uint64_t global_mem_size = 1024ULL * 1024ULL * 1024ULL;
  uint64_t local_mem_size = 64ULL * 1024ULL;

  vx_dev_caps (data->vx_device, VX_CAPS_NUM_CORES, &num_cores);
  vx_dev_caps (data->vx_device, VX_CAPS_GLOBAL_MEM_SIZE, &global_mem_size);
  vx_dev_caps (data->vx_device, VX_CAPS_LOCAL_MEM_SIZE, &local_mem_size);

  pocl_init_default_device_infos (dev, "");
  dev->vendor = "VortexGPGPU";
  dev->vendor_id = 0;
  dev->type = CL_DEVICE_TYPE_GPU;
  dev->long_name = "Vortex Open-Source GPU";
  dev->short_name = "Vortex";
  dev->image_support = CL_FALSE;

  const char *vortex_triple = pocl_get_string_option ("POCL_VORTEX_TRIPLE", NULL);
  const char *vortex_cflags = pocl_get_string_option ("POCL_VORTEX_CFLAGS", "");

  cl_bool is_64bit = CL_FALSE;
  if ((vortex_triple != NULL && strstr (vortex_triple, "64") != NULL)
      || strstr (vortex_cflags, "rv64") != NULL)
    is_64bit = CL_TRUE;

  if (vortex_triple == NULL || vortex_triple[0] == 0)
    vortex_triple = is_64bit ? "riscv64-unknown-elf" : "riscv32-unknown-elf";

  dev->address_bits = is_64bit ? 64 : 32;
  dev->has_64bit_long = is_64bit;
  dev->llvm_target_triplet = vortex_triple;
  if (dev->llvm_cpu == NULL || dev->llvm_cpu[0] == 0)
    dev->llvm_cpu = is_64bit ? "generic-rv64" : "generic-rv32";


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
                "kernel-%s-", triple);
      if (dev->kernellib_name == NULL)
        dev->kernellib_name = strdup (kernellib);
      if (dev->kernellib_fallback_name == NULL)
        dev->kernellib_fallback_name = strdup (kernellib_fallback);
    }

  dev->max_compute_units = (cl_uint)num_cores;
  dev->global_mem_size = global_mem_size;
  dev->local_mem_size = local_mem_size;
  dev->max_mem_alloc_size = global_mem_size / 2;
  dev->max_constant_buffer_size = 128UL * 1024UL;
  dev->global_mem_cache_size = 256UL * 1024UL;

  dev->autolocals_to_args = POCL_AUTOLOCALS_TO_ARGS_ALWAYS;
  dev->device_alloca_locals = CL_FALSE;

  data->is_64bit = is_64bit;

  SETUP_DEVICE_CL_VERSION (dev, 1, 2);
  pocl_set_buffer_image_limits (dev);

  dev->available = &data->available;
  dev->data = data;
  return CL_SUCCESS;
}

cl_int
pocl_vortex_uninit (unsigned j, cl_device_id dev)
{
  (void)j;

  if (dev && dev->data)
    {
      pocl_vortex_data_t *d = (pocl_vortex_data_t *)dev->data;
      if (d->vx_kernel_buffer != NULL)
        vx_mem_free (d->vx_kernel_buffer);
      if (d->vx_device != NULL)
        vx_dev_close (d->vx_device);
      POCL_DESTROY_LOCK (d->cq_lock);
      free (d);
      dev->data = NULL;
    }

  return CL_SUCCESS;
}
