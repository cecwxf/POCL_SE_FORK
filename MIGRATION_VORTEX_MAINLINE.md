# Vortex -> Mainline PoCL Migration Matrix

Branch baseline:
- Target branch: `vortex-mainline-migration`
- Source branch: `vortexgpgpu/pocl:vortex`

## Goal
Migrate Vortex runtime support from legacy fork into mainline-compatible PoCL architecture in staged PRs.

## PR-1 (asset import, no behavior switch)

| Area | Source (legacy) | Target (current) | Status | Notes |
|---|---|---|---|---|
| Driver impl | `lib/CL/devices/vortex/pocl-vortex.c` | `lib/CL/devices/vortex/pocl-vortex.c` | imported | Not wired as active driver yet |
| Driver headers | `lib/CL/devices/vortex/pocl-vortex.h` | same | imported | Uses legacy interfaces |
| Driver config | `lib/CL/devices/vortex/pocl-vortex-config.h` | same | imported | Toolchain/env knobs |
| LLVM build path | `lib/CL/pocl_llvm_build_vortex.cc` | same | imported | Requires adaptation to mainline API |
| Kernel side | `lib/kernel/vortex/*` | same | imported | Minimal files currently |
| Docs | `README.vortex` | same | imported | Legacy usage doc |
| Patch history | `vortex.patch` | same | imported | For archaeology/reference only |

## Gaps vs runnable mainline device

1. Active driver in current tree is still `lib/CL/devices/vortex/vortex.c` skeleton.
2. Submission path unresolved in skeleton (`POCL_ABORT_UNIMPLEMENTED("pocl_vortex_submit")`).
3. Legacy runtime (`pocl-vortex.c`) needs API rebase to current PoCL internals.
4. LLVM path (`pocl_llvm_build_vortex.cc`) still depends on old env/tool assumptions.

## PR-2 (planned): minimal runnable device

- Reconcile `vortex.c` skeleton with legacy `pocl-vortex.c` logic.
- Implement `submit/run/flush/join/notify` minimal command lifecycle.
- Keep scope to buffer kernels first; no image/usm initially.
- Add smoke test target for `POCL_DEVICES=vortex`.

## PR-3 (planned): toolchain and kernel pipeline

- Adapt/replace `pocl_llvm_build_vortex.cc` to current mainline interfaces.
- Normalize env knobs (`POCL_VORTEX_CFLAGS/LDFLAGS`) and failure diagnostics.
- Wire `lib/kernel/vortex` cleanly into build graph.

## PR-4 (planned): MNN integration validation

- Run MNN OpenCL demos against Vortex device path.
- Validate strict mode (`MNN_STRICT_OPENCL_NO_CPU_OP=1`) behavior.
- Add regression checklist for ops coverage and fallback detection.

