#!/usr/bin/env bash
# Benchmark replay -- the macOS/Linux twin of run_benchmarks.ps1 (Roadmap 12.5).
#
# Note the baseline in docs/benchmarking.md was measured on Windows (i5-12500H, MSVC).
# Numbers from this script are NOT comparable to that table; compare like-for-like.
#
# Usage:
#   scripts/run_benchmarks.sh [--build-dir DIR] [--config CFG] [--minutes N] [--hotpaths]
set -euo pipefail

BUILD_DIR="build-benchmarks"
CONFIG="Release"
MINUTES=180
TARGET="snapback_benchmarks"

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --minutes) MINUTES="$2"; shift 2 ;;
        --hotpaths) TARGET="snapback_hotpath_benchmarks"; shift ;;
        # Print the header comment block, not a hardcoded line range that drifts.
        -h|--help) awk 'NR>1 && /^#/ {sub(/^# ?/,""); print; next} NR>1 {exit}' "$0"; exit 0 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_PATH="$REPO_ROOT/$BUILD_DIR"

command -v cmake >/dev/null 2>&1 || { echo "cmake is required but was not found on PATH." >&2; exit 1; }

echo "== Configure benchmarks =="
cmake -S "$REPO_ROOT" -B "$BUILD_PATH" \
    -DCMAKE_BUILD_TYPE="$CONFIG" \
    -DSNAPBACK_BUILD_APP=OFF \
    -DSNAPBACK_ONNX=OFF \
    -DSNAPBACK_BUILD_BENCHMARKS=ON

echo "== Build $TARGET =="
cmake --build "$BUILD_PATH" --config "$CONFIG" --target "$TARGET" --parallel

# Multi-config generators nest the binary under the config; single-config ones do not.
EXE="$BUILD_PATH/$CONFIG/$TARGET"
[ -x "$EXE" ] || EXE="$BUILD_PATH/$TARGET"
[ -x "$EXE" ] || { echo "Benchmark executable not found under $BUILD_PATH." >&2; exit 1; }

echo "== Run $TARGET =="
SNAPBACK_BENCH_MINUTES="$MINUTES" "$EXE"
