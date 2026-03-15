# Vortex on Mainline PoCL - Baselines

## Goal
Port Vortex GPGPU support from legacy `vortexgpgpu/pocl` branch into current `pocl/pocl` mainline incrementally.

## Fixed Baselines
- Mainline PoCL target: `origin/main` (checked out branch: `vortex-mainline-migration`)
- Legacy Vortex source: `https://github.com/vortexgpgpu/pocl` branch `vortex`
- Legacy Vortex LLVM source: `https://github.com/vortexgpgpu/llvm` branch `vortex`

## Phase Deliverables
1. API/Files diff map (`migration/API_DIFF.md`)
2. MVP checklist (`migration/MVP_CHECKLIST.md`)
3. Automated sync helper (`scripts/vortex/collect_diff.sh`)

## Non-goals (Phase 0)
- No broad refactor of existing devices
- No performance tuning
- No image/fp16 optimization paths
