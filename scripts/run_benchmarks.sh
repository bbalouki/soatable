#!/usr/bin/env bash
# Build and run the SoaTable benchmark suite via the `bench` CMake preset, recording the toolchain
# and emitting a machine-readable results file. Google Benchmark prints CPU topology and cache sizes
# on startup, so the output is self-describing.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${ROOT}"

OUT_DIR="${ROOT}/bench"
mkdir -p "${OUT_DIR}"

cmake --preset bench
cmake --build --preset bench

BENCH_BIN="$(find "${ROOT}/build/bench" -name 'soatable_benchmark*' -type f -perm -u+x | head -n1)"
if [[ -z "${BENCH_BIN}" ]]; then
    echo "Benchmark binary not found under build/bench" >&2
    exit 1
fi

# Record the build environment alongside the results so numbers stay reproducible/comparable.
{
    echo "date: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
    echo "cmake: $(cmake --version | head -n1)"
    echo "compiler: ${CXX:-default}"
    echo "build_type: Release"
} > "${OUT_DIR}/environment.txt"

"${BENCH_BIN}" \
    --benchmark_out="${OUT_DIR}/results.json" \
    --benchmark_out_format=json

echo "Wrote ${OUT_DIR}/results.json and ${OUT_DIR}/environment.txt"
