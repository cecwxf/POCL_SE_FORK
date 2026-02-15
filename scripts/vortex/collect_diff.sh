#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
WORK_DIR="${ROOT_DIR}/.migration_tmp"
UP_MAIN="https://github.com/pocl/pocl.git"
UP_VORTEX="https://github.com/vortexgpgpu/pocl.git"
MAIN_DIR="${WORK_DIR}/pocl-main"
VORTEX_DIR="${WORK_DIR}/pocl-vortex"
OUT="${ROOT_DIR}/migration/API_DIFF.md"

rm -rf "${WORK_DIR}"
mkdir -p "${WORK_DIR}"

git clone --depth 1 "${UP_MAIN}" "${MAIN_DIR}" >/dev/null 2>&1
git clone --depth 1 --branch vortex "${UP_VORTEX}" "${VORTEX_DIR}" >/dev/null 2>&1

{
  echo "# API / Files Diff: mainline PoCL vs legacy Vortex fork"
  echo
  echo "Generated: $(date -Iseconds)"
  echo

  echo "## Legacy Vortex-specific paths"
  find "${VORTEX_DIR}" -path '*/.git*' -prune -o -type f -print \
    | xargs grep -Il "vortex" \
    | sed "s#${VORTEX_DIR}/##" \
    | sort -u \
    | sed 's/^/- /' \
    | head -n 300
  echo

  echo "## Paths present only in legacy Vortex fork (top 200)"
  comm -13 \
    <(cd "${MAIN_DIR}" && find . -path './.git*' -prune -o -type f -print | sort) \
    <(cd "${VORTEX_DIR}" && find . -path './.git*' -prune -o -type f -print | sort) \
    | sed 's#^./#- #' | head -n 200
  echo

  echo "## CMake references to Vortex"
  grep -RIn "ENABLE_VORTEX\|vortex" \
    "${VORTEX_DIR}/CMakeLists.txt" "${VORTEX_DIR}/lib/CL/CMakeLists.txt" 2>/dev/null \
    | sed "s#${VORTEX_DIR}/##"
  echo

  echo "## Device driver entry points (legacy)"
  grep -RIn "pocl_vortex_.*(\|device_name = \"vortex\"" \
    "${VORTEX_DIR}/lib/CL/devices/vortex" 2>/dev/null \
    | sed "s#${VORTEX_DIR}/##" | head -n 200
} > "${OUT}"

echo "Wrote ${OUT}"
