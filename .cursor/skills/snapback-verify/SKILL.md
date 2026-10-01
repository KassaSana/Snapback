---
name: snapback-verify
description: >-
  Runs Snapback verification with scripts/verify.py (full suite, native CTest
  regex, or a single frontend test file). Use when finishing a change, claiming
  tests pass, debugging a red build, or choosing which check to run.
---

# Snapback verify

From the repository root:

```text
python scripts/verify.py
python scripts/verify.py native "CaptureThread"
python scripts/verify.py frontend-unit sessionStatus.test.ts
python scripts/verify.py frontend-component sessionFlow.test.tsx
python scripts/verify.py guards
```

## Rules

- Prefer a targeted mode while iterating; run the no-argument suite before finishing.
- Frontend modes require an **existing** file under `frontend/tests/`.
- Native selection is a CTest case-name regex; discover names with `ctest --test-dir build -N`.
- Never claim behavior is verified without naming what ran and what failed or passed.
- Investigate failures; do not ignore red checks or invent "expected on this machine" excuses.
- Desktop GUI, sanitizers, and deep checks are CI-only relative to the local headless suite; see `docs/testing_strategy.md`.
