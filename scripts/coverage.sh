#!/usr/bin/env bash
# scripts/coverage.sh
#
# Refine-the-architecture plan, Phase 10a: configure, build and run the unit
# suite with -DNCVIEW_COVERAGE=ON (GCC/gcov instrumentation, see the root
# CMakeLists.txt), then summarize per-file coverage with gcovr. GCC/gcov
# rather than Clang/llvm-cov because every CI job that could plausibly run
# this (linux, sanitize) already builds with GCC (ubuntu-latest's default
# cc/c++).
#
# Usage: scripts/coverage.sh [build-dir]
#   build-dir defaults to build-coverage (kept separate from the normal
#   build/ tree so a developer's ordinary build is never coverage-instrumented
#   by accident, and so re-running this script doesn't force a full rebuild
#   of an unrelated configuration).
#
# Requires gcovr (pip install gcovr, or apt-get install gcovr on Debian/
# Ubuntu). CMake target/Ninja-vs-Make agnostic: this is a plain script, not a
# `make coverage` rule, since the project supports both generators.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

BUILD_DIR="${1:-build-coverage}"
REPORT_DIR="${BUILD_DIR}/coverage-report"

if ! command -v gcovr >/dev/null 2>&1; then
	echo "error: gcovr not found on PATH -- install it (pip install gcovr, or apt-get install gcovr) first" >&2
	exit 1
fi

echo "== Configuring (${BUILD_DIR}) =="
# -DFLTK_BACKEND_WAYLAND=OFF -DFLTK_BUILD_GL=OFF: this project always forces
# FLTK_BACKEND=x11 at runtime (see README) and never uses OpenGL, matching
# every other CI job's configure line -- harmless locally, and avoids
# pulling in Wayland/GL dev packages a coverage-only CI runner won't have.
cmake -S . -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug -DNCVIEW_COVERAGE=ON \
	-DFLTK_BACKEND_WAYLAND=OFF -DFLTK_BUILD_GL=OFF

echo "== Building ncview_core_tests =="
cmake --build "${BUILD_DIR}" -j"$(nproc)" --target ncview_core_tests

echo "== Running the unit suite (generates .gcda files) =="
"${BUILD_DIR}/tests/ncview_core_tests"

echo "== Summarizing coverage (core/src only) =="
mkdir -p "${REPORT_DIR}"
gcovr \
	--root . \
	--filter 'core/src/.*' \
	--exclude '.*/third_party/.*' \
	--print-summary \
	--txt "${REPORT_DIR}/summary.txt" \
	--html-details "${REPORT_DIR}/index.html" \
	--json-summary-pretty -o "${REPORT_DIR}/summary.json" \
	"${BUILD_DIR}"

echo
echo "Per-file report: ${REPORT_DIR}/summary.txt"
echo "HTML report:     ${REPORT_DIR}/index.html"
