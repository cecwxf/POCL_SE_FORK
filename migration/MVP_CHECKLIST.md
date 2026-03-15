# MVP Checklist: PoCL Mainline + Vortex

## Stage A - Baseline and Diff
- [x] Create migration branch
- [x] Add baseline document
- [x] Generate file-level diff from legacy Vortex fork
- [ ] Identify device op signature changes

## Stage B - Minimal Device Bring-up
- [ ] Add `ENABLE_VORTEX` build option in mainline
- [ ] Add vortex device skeleton compile target
- [ ] Make `clGetPlatformIDs` + device enumeration show Vortex device
- [ ] Run one buffer kernel (vector add)

## Stage C - MNN Smoke Integration
- [ ] Build MNN OpenCL with new mainline PoCL+Vortex runtime
- [ ] Run tiny model (`tiny_matmul_add.mnn`)
- [ ] Run with `MNN_STRICT_OPENCL_NO_CPU_OP=1`

## Acceptance Criteria
- Vortex device appears via OpenCL enumeration
- vector add kernel passes correctness
- tiny MNN model runs correctly without per-op CPU fallback
