---
name: snapback-ipc-contract
description: >-
  Keeps Snapback native IPC commands and events in sync across registration,
  fixtures, frontend API, and the browser demo backend. Use when adding,
  renaming, or changing a command/event, editing command_handlers.cpp,
  ipc_commands.json, api.ts, or demo/backend.ts.
---

# Snapback IPC contract

Adding or renaming a native command/event requires **all** of:

1. `src/app/command_handlers.cpp` — registration / handler
2. `fixtures/ipc_commands.json` — and the expected count in `tests/test_ipc_contract.cpp`
3. `frontend/src/api.ts` — typed frontend calls
4. `frontend/demo/backend.ts` — in-memory demo handler (same names/shapes)

## Rules

- IPC keys: **camelCase**. Command names: **snake_case**.
- Training/fixture `CaptureEvent` JSON stays snake_case; do not "fix" that to match IPC.
- Contract tests name the forgotten file; fix the four-way sync rather than weakening the test.
- Demo disk/native-only ops may return "unavailable in the browser demo"; still register the command name.
- After changes, run at least `python scripts/verify.py native "ipc"` (or the closest contract test name) and a relevant frontend check if API shapes moved.
