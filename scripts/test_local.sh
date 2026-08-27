#!/usr/bin/env bash
# Local mock/headless test suite -- the macOS/Linux twin of test_local.ps1.
#
# Roadmap 12.5: the development host is macOS, and every documented way to run the suite
# was PowerShell. This is the same pipeline (C++ headless tests, then the frontend) with
# one real difference: MSVC is a *multi-config* generator, so the .ps1 passes --config at
# build time and finds binaries under build/Release/. Make and Ninja are single-config, so
# the build type must be chosen at *configure* time via CMAKE_BUILD_TYPE, and binaries land
# directly in build/. Both layouts are handled below.
#
# Usage:
#   scripts/test_local.sh [--build-dir DIR] [--config CFG] [--skip-frontend] [--skip-npm-install]
#                         [--include-benchmark-smoke]
set -euo pipefail

BUILD_DIR="build"
CONFIG="Release"
SKIP_FRONTEND=0
SKIP_NPM_INSTALL=0
INCLUDE_BENCHMARK_SMOKE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --skip-frontend) SKIP_FRONTEND=1; shift ;;
        --skip-npm-install) SKIP_NPM_INSTALL=1; shift ;;
        --include-benchmark-smoke) INCLUDE_BENCHMARK_SMOKE=1; shift ;;
        # Print the header comment block, not a hardcoded line range that drifts.
        -h|--help) awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_PATH="$REPO_ROOT/$BUILD_DIR"

require() {
    command -v "$1" >/dev/null 2>&1 || { echo "$1 is required but was not found on PATH." >&2; exit 1; }
}

require cmake
require ctest

echo "== C++ mock/headless tests =="
# SNAPBACK_BUILD_APP=OFF keeps webview out of it (no desktop session needed);
# SNAPBACK_ONNX=OFF because third_party/onnxruntime is not vendored locally.
cmake -S "$REPO_ROOT" -B "$BUILD_PATH" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DSNAPBACK_BUILD_APP=OFF \
    -DSNAPBACK_ONNX=OFF \n    -DSNAPBACK_BUILD_BENCHMARKS=ON
cmake --build "$BUILD_PATH" --config "$CONFIG" --target snapback_tests --parallel
ctest --test-dir "$BUILD_PATH" -C "$CONFIG" --output-on-failure

# Compiled every run, deliberately, even though the smoke run below is opt-in.
#
# benchmarks/ drives the same private seams the tests do, so a type change in src/ breaks it
# the same way -- but SNAPBACK_BUILD_BENCHMARKS defaults OFF, so nothing here used to compile
# these files and CI's benchmark job was the first thing that did. ADR-0007's timestamp_ms
# rename landed on a green local run and turned master red for exactly that reason. Building
# is seconds; it is the running that costs minutes.
#
# Both targets, unlike CI: the benchmark-smoke job builds only snapback_benchmarks, so
# bench_hotpaths.cpp has no build coverage anywhere else.
echo "== Benchmark build (compile-only unless --include-benchmark-smoke) =="
cmake --build "$BUILD_PATH" --config "$CONFIG"     --target snapback_benchmarks snapback_hotpath_benchmarks --parallel

if [ "$INCLUDE_BENCHMARK_SMOKE" -eq 1 ]; then
    echo "== Benchmark smoke =="
    # Multi-config generators nest the binary under a per-config directory; single-config
    # ones put it straight in the build root. See the header note.
    BENCH="$BUILD_PATH/$CONFIG/snapback_benchmarks"
    [ -x "$BENCH" ] || BENCH="$BUILD_PATH/snapback_benchmarks"
    [ -x "$BENCH" ] || { echo "benchmark binary not found under $BUILD_PATH" >&2; exit 1; }
    # One simulated minute: enough to exercise every replay path, short enough to sit through.
    SNAPBACK_BENCH_MINUTES=1 "$BENCH"
fi

if [ "$SKIP_FRONTEND" -eq 0 ]; then
    require npm
    echo "== Frontend mock tests =="
    (
        cd "$REPO_ROOT/frontend"
        if [ ! -d node_modules ] && [ "$SKIP_NPM_INSTALL" -eq 0 ]; then
            npm ci
        fi
        npm run typecheck
        npm run test
        npm run build
    )
fi

# No --include-windows-demo equivalent: windows_demo.ps1 needs MSVC and produces
# snapback.exe, so it cannot run here. That path is CI-only off Windows.
echo "Local test suite completed."
