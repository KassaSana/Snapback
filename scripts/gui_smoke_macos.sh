#!/usr/bin/env bash
#
# macOS GUI launch smoke — the macOS counterpart to scripts/gui_smoke_windows.ps1.
#
# ADR-0002 lists "a macOS launch smoke in CI" as a v1 release blocker because every macOS
# job we had configured the app with SNAPBACK_BUILD_APP=ON and then never launched it. That
# proves the binary links; it does not prove it starts. The two failures that gap allowed
# through are both real and both silent at link time: a webview that cannot create its
# window, and a frontend bundle that never got copied next to the executable, which renders
# an empty window rather than an error.
#
# What this asserts, in order of what it would catch:
#
#   1. The process starts and stays up long enough to reach its run loop.
#   2. SNAPBACK_GUI_SESSION_SMOKE's round trip completes — a session is started and stopped
#      through AppState and SQLite from the UI thread, and the marker names it. This is what
#      makes it a *launch* smoke rather than a "did the process exist" check.
#   3. The run loop exits on its own when terminate() is called, rather than being killed.
#      That is the same path the tray's Quit item drives, and a hang there strands the app
#      with no way out but Force Quit.
#   4. The webview navigated to a real file:// bundle. main.cpp logs the URL precisely
#      because `about:blank` and a malformed `file:////` path both look like an empty window
#      and differ only in that string.
#
# There is no window-title check like the Windows script's MainWindowTitle probe: reading
# another process's windows on macOS needs Accessibility permission, which a CI runner
# cannot grant unattended. Assertion 2 covers the same ground more directly anyway — the
# marker can only be written from a dispatch on the running UI loop.
#
# Usage: scripts/gui_smoke_macos.sh [--no-build] [--skip-frontend] [--timeout N]

set -euo pipefail

BUILD_DIR="build-macos-smoke"
TIMEOUT_SECONDS=90
DO_BUILD=1
DO_FRONTEND=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --no-build) DO_BUILD=0; shift ;;
        --skip-frontend) DO_FRONTEND=0; shift ;;
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --timeout) TIMEOUT_SECONDS="$2"; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 2 ;;
    esac
done

if [[ "$(uname -s)" != "Darwin" ]]; then
    echo "gui_smoke_macos.sh only runs on macOS (uname -s was $(uname -s))." >&2
    exit 2
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_PATH="$REPO_ROOT/$BUILD_DIR"
DATA_DIR="$REPO_ROOT/.demo/gui-smoke-data-macos"
MARKER="$DATA_DIR/gui_session_smoke.ok"
APP_LOG="$DATA_DIR/snapback.log"

cd "$REPO_ROOT"

# The frontend bundle has to exist *before* CMake configures: the copy that puts
# frontend/dist next to the executable is wired at configure time, so building the bundle
# afterwards would leave the app pointed at nothing and assertion 3 would fail.
if [[ "$DO_FRONTEND" == "1" ]]; then
    echo "==> building frontend bundle"
    (cd frontend && npm ci && npm run build)
fi

if [[ ! -f frontend/dist/index.html ]]; then
    echo "frontend/dist/index.html is missing; run without --skip-frontend." >&2
    exit 1
fi

if [[ "$DO_BUILD" == "1" ]]; then
    echo "==> building snapback (SNAPBACK_BUILD_APP=ON)"
    cmake -S . -B "$BUILD_PATH" -DCMAKE_BUILD_TYPE=Release -DSNAPBACK_BUILD_APP=ON
    cmake --build "$BUILD_PATH" --target snapback --parallel
fi

EXE="$BUILD_PATH/snapback"
if [[ ! -x "$EXE" ]]; then
    echo "snapback was not found at $EXE." >&2
    exit 1
fi

# Wipe the data dir rather than reusing it. A marker left by a previous run would let this
# script pass without the app ever starting, which is the one way a smoke test can be worse
# than no smoke test at all.
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR"

export SNAPBACK_DATA_DIR="$DATA_DIR"
export SNAPBACK_GUI_SESSION_SMOKE=1
unset SNAPBACK_FRONTEND_URL || true
unset SNAPBACK_OVERLAY_TEST || true

echo "==> launching $EXE"
"$EXE" >"$DATA_DIR/stdout.log" 2>&1 &
APP_PID=$!

# Safety net only. A passing run exits on its own; anything this has to kill is a bug the
# assertions below are meant to have already reported.
cleanup() {
    if kill -0 "$APP_PID" 2>/dev/null; then
        kill "$APP_PID" 2>/dev/null || true
        sleep 1
        kill -9 "$APP_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

deadline=$(( $(date +%s) + TIMEOUT_SECONDS ))
while [[ ! -f "$MARKER" ]]; do
    if [[ $(date +%s) -ge $deadline ]]; then
        echo "FAIL: no session marker at $MARKER within ${TIMEOUT_SECONDS}s." >&2
        [[ -f "$APP_LOG" ]] && { echo "--- snapback.log ---" >&2; cat "$APP_LOG" >&2; }
        echo "--- stdout ---" >&2; cat "$DATA_DIR/stdout.log" >&2
        exit 1
    fi
    # An early exit is a distinct failure from a hang, and the exit code says which.
    # Checked after the marker test so a clean, fast run is not reported as a crash.
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        wait "$APP_PID" && status=0 || status=$?
        if [[ ! -f "$MARKER" ]]; then
            echo "FAIL: snapback exited with code $status before writing the marker." >&2
            [[ -f "$APP_LOG" ]] && { echo "--- snapback.log ---" >&2; cat "$APP_LOG" >&2; }
            echo "--- stdout ---" >&2; cat "$DATA_DIR/stdout.log" >&2
            exit 1
        fi
        break
    fi
    sleep 0.25
done

SESSION_ID="$(cat "$MARKER")"
if [[ -z "$SESSION_ID" ]]; then
    echo "FAIL: session marker at $MARKER is empty." >&2
    exit 1
fi

# The smoke hook calls webview::terminate() right after writing the marker, so the process
# must now wind down by itself. Asserting that instead of just SIGTERM-ing it is what keeps
# a run loop that ignores terminate() from passing — the same path the tray's Quit item
# uses, and a hang there would strand the app with no way out but Force Quit.
shutdown_deadline=$(( $(date +%s) + 15 ))
while kill -0 "$APP_PID" 2>/dev/null; do
    if [[ $(date +%s) -ge $shutdown_deadline ]]; then
        echo "FAIL: snapback wrote the marker but its run loop did not exit within 15s." >&2
        exit 1
    fi
    sleep 0.25
done
wait "$APP_PID" && exit_status=0 || exit_status=$?
if [[ "$exit_status" != "0" ]]; then
    echo "FAIL: snapback exited with code $exit_status after a successful session round trip." >&2
    [[ -f "$APP_LOG" ]] && { echo "--- snapback.log ---" >&2; cat "$APP_LOG" >&2; }
    exit 1
fi

if [[ ! -f "$APP_LOG" ]]; then
    echo "FAIL: no app log at $APP_LOG." >&2
    exit 1
fi

if ! grep -q "navigating webview to: file://" "$APP_LOG"; then
    echo "FAIL: the webview did not navigate to a file:// bundle." >&2
    grep "navigating webview to:" "$APP_LOG" >&2 || echo "(no navigation line at all)" >&2
    exit 1
fi

if grep -q "no frontend bundle next to the executable" "$APP_LOG"; then
    echo "FAIL: the app launched without its frontend bundle — the window would be empty." >&2
    exit 1
fi

echo "PASS: snapback launched, ran session $SESSION_ID through storage, and loaded its bundle."
