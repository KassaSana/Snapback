# System architecture

Snapback is a single native C++ process with a React dashboard rendered in the
system webview. The current module map and runtime boundaries are documented in
[ARCHITECTURE.md](ARCHITECTURE.md); this page records the main design decisions.

## Design decisions

| Decision | Reason |
| --- | --- |
| One native process | Capture, inference, persistence, and UI events share clear ownership boundaries. |
| SPSC ring buffer | The OS callback stays fast and allocation-free while the engine consumes events independently. |
| Heuristic classifier by default | The app remains useful without a model artifact. |
| ONNX as an optional backend | Model inference is isolated behind a small classifier seam. |
| SQLite with short transactions | Durable history is local and writes do not hold locks across UI work. |
| Project-owned browser bridge | The frontend depends only on the commands and events this app exposes. |
| Exact feature-vector fixtures | A checked-in golden file catches input-order and calculation drift. |

## Startup

`main.cpp` resolves paths, opens storage, constructs `AppState`, starts the
engine, injects the bridge, registers commands, and navigates the frontend.
Failures during startup are logged and stop the process before a partially
initialized UI is presented.

## Error boundaries

Native command handlers throw for invalid input or failed operations. The common
dispatcher catches exceptions and returns a JSON error envelope. The frontend
bridge rejects the corresponding promise, so UI code can use ordinary async
error handling.

## Verification

The C++ suite covers the core modules, IPC validation, platform-independent
helpers, and exact feature vectors. The frontend suite covers mappers, hooks,
flows, and the bridge boundary. CI runs the same tests on the supported runners,
with sanitizer jobs for concurrency and lifetime-sensitive code.
