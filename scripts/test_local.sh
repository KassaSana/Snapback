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
set -euo pipefail

BUILD_DIR="build"
CONFIG="Release"
SKIP_FRONTEND=0
SKIP_NPM_INSTALL=0

while [ $# -gt 0 ]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --config) CONFIG="$2"; shift 2 ;;
        --skip-frontend) SKIP_FRONTEND=1; shift ;;
        --skip-npm-install) SKIP_NPM_INSTALL=1; shift ;;
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
    -DSNAPBACK_ONNX=OFF
cmake --build "$BUILD_PATH" --config "$CONFIG" --target snapback_tests --parallel
ctest --test-dir "$BUILD_PATH" -C "$CONFIG" --output-on-failure

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
