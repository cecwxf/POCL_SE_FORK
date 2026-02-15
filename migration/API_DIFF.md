# API / Files Diff: mainline PoCL vs legacy Vortex fork

Generated: 2026-02-15T21:13:27+08:00

## Legacy Vortex-specific paths
- CMakeLists.txt
- dogfood/vecadd2/main.cc
- dogfood/vecadd2/Makefile
- dogfood/vecadd3/main.cc
- lib/CL/CMakeLists.txt
- lib/CL/devices/CMakeLists.txt
- lib/CL/devices/common.c
- lib/CL/devices/devices.c
- lib/CL/devices/vortex/CMakeLists.txt
- lib/CL/devices/vortex/pocl-vortex.c
- lib/CL/devices/vortex/pocl-vortex-config.h
- lib/CL/devices/vortex/pocl-vortex.h
- lib/CL/pocl_llvm_build.cc
- lib/CL/pocl_llvm_build_vortex.cc
- lib/CL/pocl_llvm.h
- lib/CL/pocl_llvm_wg.cc
- lib/kernel/CMakeLists.txt
- lib/kernel/vortex/CMakeLists.txt
- lib/llvmopencl/VortexAttribute.cc
- lib/llvmopencl/VortexBarrier.cc
- lib/llvmopencl/VortexPrintf.cc
- lib/llvmopencl/WorkitemLoops.cc
- lib/llvmopencl/WorkitemLoopsVX.cc
- README.vortex
- vortex.patch

## Paths present only in legacy Vortex fork (top 200)
- benchmarks/nearn/cane4_0.db
- benchmarks/nearn/cane4_1.db
- benchmarks/nearn/cane4_2.db
- benchmarks/nearn/cane4_3.db
- benchmarks/nearn/clutils.cpp
- benchmarks/nearn/clutils.h
- benchmarks/nearn/filelist.txt
- benchmarks/nearn/ipoint.h
- benchmarks/nearn/kernel.cl
- benchmarks/nearn/kernel.spv
- benchmarks/nearn/main.cc
- benchmarks/nearn/nearestNeighbor.h
- benchmarks/nearn/README.txt
- benchmarks/nearn/run
- benchmarks/nearn/utils.cpp
- benchmarks/nearn/utils.h
- benchmarks/saxpy/kernel.cl
- benchmarks/saxpy/kernel.pocl
- benchmarks/saxpy/kernel.spv
- benchmarks/saxpy/main.cc
- benchmarks/saxpy/README
- benchmarks/sfilter/kernel.cl
- benchmarks/sfilter/kernel.spv
- benchmarks/sfilter/main.cc
- benchmarks/sfilter/README
- benchmarks/sgemm/kernel.cl
- benchmarks/sgemm/kernel.spv
- benchmarks/sgemm/main.cc
- benchmarks/sgemm/README
- benchmarks/vecadd/kernel.cl
- benchmarks/vecadd/kernel.spv
- benchmarks/vecadd/main.cc
- benchmarks/vecadd/README
- cmake/make_opaque_ptr.cmake
- doc/www/development.mak
- doc/www/discussion.mak
- dogfood/copy_llvm_libs.sh
- dogfood/env.sh
- dogfood/hello.c
- dogfood/hello.cpp
- dogfood/vecadd2/kernel.cl
- dogfood/vecadd2/main.cc
- dogfood/vecadd2/Makefile
- dogfood/vecadd3/kernel.cl
- dogfood/vecadd3/main.cc
- dogfood/vecadd3/prebuilt.sh
- dogfood/vecadd3/saxpy.cu
- dogfood/vecadd3/saxpy.spv
- dogfood/vecadd3/sfilter.cu
- dogfood/vecadd3/sfilter.spv
- dogfood/vecadd3/sgemm.cu
- dogfood/vecadd3/sgemm.spv
- dogfood/vecadd3/vecadd.cu
- dogfood/vecadd3/vecadd.spv
- dogfood/vecadd/kernel.cl
- dogfood/vecadd/main.cc
- dogfood/vecadd/Makefile
- examples/AMD/AMDSDK.patch
- examples/AMD/CMakeLists.txt
- examples/AMD/README
- examples/AMDSDK2.9/AMDSDK2_9.patch
- examples/AMDSDK2.9/CMakeLists.txt
- examples/AMDSDK2.9/README
- examples/chipStar/failed_tests_fp16_disabled.txt
- examples/chipStar/failed_tests.txt
- examples/chipStar/HIP_tests_catch.patch
- examples/dpcpp-book-samples/dpcpp.patch
- examples/piglit/produce_results.sh
- examples/piglit/README
- examples/piglit/sorted_ref
- examples/piglit/sorted_ref_llvm_3.5
- include/hpp/CL/cl2.hpp
- include/hpp/CL/CMakeLists.txt
- include/hpp/CL/opencl.hpp
- include/_libclang_versions_checks.h
- include/pocl_file_util.h
- lib/CL/devices/almaif/openasip/AlmaifCompileTCE.cc
- lib/CL/devices/almaif/openasip/AlmaifCompileTCE.hh
- lib/CL/devices/almaif/XrtDevice.cc
- lib/CL/devices/almaif/XrtDevice.hh
- lib/CL/devices/almaif/XrtRegion.cc
- lib/CL/devices/almaif/XrtRegion.hh
- lib/CL/devices/builtin_kernels.cc
- lib/CL/devices/builtin_kernels.hh
- lib/CL/devices/proxy/pocl_proxy.c
- lib/CL/devices/proxy/pocl_proxy.h
- lib/CL/devices/pthread/pocl-pthread_utils.h
- lib/CL/devices/pthread/pthread_barrier.c
- lib/CL/devices/pthread/pthread_barrier.h
- lib/CL/devices/pthread/pthread_utils.c
- lib/CL/devices/vortex/CMakeLists.txt
- lib/CL/devices/vortex/pocl-vortex.c
- lib/CL/devices/vortex/pocl-vortex-config.h
- lib/CL/devices/vortex/pocl-vortex.h
- lib/CL/pocl_llvm_build_vortex.cc
- lib/kernel/addrspace_operators.ll
- lib/kernel/errol/enum3.h
- lib/kernel/errol/enum4.h
- lib/kernel/errol/errol.c
- lib/kernel/errol/errol.h
- lib/kernel/errol/itoa_c.h
- lib/kernel/errol/LICENSE.txt
- lib/kernel/errol/lookup.h
- lib/kernel/errol/README
- lib/kernel/get_linear_id.c
- lib/kernel/printf_base.c
- lib/kernel/printf_base.h
- lib/kernel/printf_constant.c
- lib/kernel/subgroups.c
- lib/kernel/vortex/CMakeLists.txt
- lib/kernel/vortex/printf.c
- lib/llvmopencl/BreakConstantGEPs.cpp
- lib/llvmopencl/BreakConstantGEPs.h
- lib/llvmopencl/CompilerWarnings.h
- lib/llvmopencl/OptimizeWorkItemFuncCalls.cc
- lib/llvmopencl/OptimizeWorkItemFuncCalls.h
- lib/llvmopencl/PrintModule.cc
- lib/llvmopencl/RemoveOptnoneFromWIFunc.cc
- lib/llvmopencl/RemoveOptnoneFromWIFunc.h
- lib/llvmopencl/UnifyPrintf.cc
- lib/llvmopencl/UnifyPrintf.h
- lib/llvmopencl/VortexAttribute.cc
- lib/llvmopencl/VortexBarrier.cc
- lib/llvmopencl/VortexPrintf.cc
- lib/llvmopencl/WorkitemLoopsVX.cc
- lib/llvmopencl/WorkitemReplication.cc
- lib/llvmopencl/WorkitemReplication.h
- LICENSE
- LICENSE_THIRDPARTY
- pocl_cl.h
- poclu/cl_half.c
- README.ARM
- README.packaging
- README.PPC64le
- README.riscv-linux
- README.vortex
- README.Windows
- RISCV_linux64.cmake
- RISCV_linux.cmake
- RISCV_newlib.cmake
- TODO
- tools/data/test_machine.adf
- tools/data/test_machine_fp16.adf
- tools/docker/ArchLinux/default
- tools/docker/ArchLinux/distro
- tools/docker/Debian/bullseye
- tools/docker/Ubuntu/20_04.64bit
- tools/docker/Ubuntu/22_04.64bit
- tools/docker/Ubuntu/conformance.64bit
- tools/docker/Ubuntu/distro.64bit
- tools/docker/Ubuntu/ocl-icd_ubuntu_ppa.gpg
- tools/docker/Ubuntu/ocl-icd-ubuntu-ppa.list
- tools/patches/khronos_cl.hpp.patch
- tools/patches/khronos-icd-loader.patch
- tools/patches/replace-kernels-on-the-fly.patch
- tools/scripts/babelparse.rb
- tools/scripts/run_all_tests
- tools/scripts/run_tta_tests
- tools/uncrustify_cxx.cfg
- .travis.yml
- vortex.patch
- .vscode/launch.json
- windows/setup_and_build_win64.sh

## CMake references to Vortex
CMakeLists.txt:244:option(ENABLE_VORTEX "Enable the Vortex device driver" OFF)
CMakeLists.txt:252:if (ENABLE_VORTEX)
CMakeLists.txt:1750:if (ENABLE_VORTEX)
CMakeLists.txt:1751:  set(OCL_DRIVERS "${OCL_DRIVERS} vortex")
CMakeLists.txt:1752:  set(OCL_TARGETS "${OCL_TARGETS} vortex")
CMakeLists.txt:2240:MESSAGE(STATUS "ENABLE_VORTEX: ${ENABLE_VORTEX}")
lib/CL/CMakeLists.txt:215:  set(LLVM_API_SOURCES "pocl_llvm_build.cc" "pocl_llvm_metadata.cc" "pocl_llvm_utils.cc" "pocl_llvm_wg.cc" "pocl_llvm_build_vortex.cc")

## Device driver entry points (legacy)
lib/CL/devices/vortex/pocl-vortex.c:147:pocl_vortex_init_device_ops(struct pocl_device_ops *ops)
lib/CL/devices/vortex/pocl-vortex.c:149:  ops->device_name = "vortex";
lib/CL/devices/vortex/pocl-vortex.c:229:pocl_vortex_build_hash (cl_device_id device)
lib/CL/devices/vortex/pocl-vortex.c:242:pocl_vortex_probe(struct pocl_device_ops *ops)
lib/CL/devices/vortex/pocl-vortex.c:250:pocl_vortex_init (unsigned j, cl_device_id dev, const char* parameters)
lib/CL/devices/vortex/pocl-vortex.c:402:cl_int pocl_vortex_uninit (unsigned j, cl_device_id device) {
lib/CL/devices/vortex/pocl-vortex.c:423:cl_int pocl_vortex_alloc_mem_obj(cl_device_id device, cl_mem mem_obj, void *host_ptr) {
lib/CL/devices/vortex/pocl-vortex.c:477:void pocl_vortex_free(cl_device_id device, cl_mem mem_obj) {
lib/CL/devices/vortex/pocl-vortex.c:495:void pocl_vortex_write(void *data,
lib/CL/devices/vortex/pocl-vortex.c:509:void pocl_vortex_read(void *data,
lib/CL/devices/vortex/pocl-vortex.c:523:cl_int pocl_vortex_map_mem (void *data, pocl_mem_identifier *src_mem_id,
lib/CL/devices/vortex/pocl-vortex.c:538:cl_int pocl_vortex_unmap_mem (void *data, pocl_mem_identifier *dst_mem_id,
lib/CL/devices/vortex/pocl-vortex.c:572:pocl_vortex_submit (_cl_command_node *node, cl_command_queue cq)
lib/CL/devices/vortex/pocl-vortex.c:590:void pocl_vortex_flush (cl_device_id device, cl_command_queue cq)
lib/CL/devices/vortex/pocl-vortex.c:600:pocl_vortex_join (cl_device_id device, cl_command_queue cq)
lib/CL/devices/vortex/pocl-vortex.c:612:pocl_vortex_notify (cl_device_id device, cl_event event, cl_event finished)
lib/CL/devices/vortex/pocl-vortex.c:642:pocl_vortex_run (void *data, _cl_command_node *cmd)
lib/CL/devices/vortex/pocl-vortex.c:878:void pocl_vortex_compile_kernel(_cl_command_node *cmd, cl_kernel kernel,
