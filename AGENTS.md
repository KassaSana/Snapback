# Agent guidance for Snapback

This is the tool-neutral entry point for coding agents (Codex, Cursor, Gemini CLI, Claude
Code, and whatever comes next). It is committed so every clone has it; it is short so it
cannot drift from the documents it points at. Do not duplicate their content here.

## Read first

1. [`CONTRIBUTING.md`](CONTRIBUTING.md) — the conventions that are enforced but not obvious.
   Read all of it before the first change.
2. [`docs/running.md`](docs/running.md) — per-OS build, test, and run commands.
3. [`docs/testing_strategy.md`](docs/testing_strategy.md) — what each CI job proves.
4. [`docs/ROADMAP.md`](docs/ROADMAP.md) — where work is tracked; pick items from here and
   record progress in the item, as its neighbours do.
5. [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) and [`docs/adr/`](docs/adr/) — the shape
   of the system and the decisions behind it.

## The one rule with no exceptions

Every commit is the repository owner's own work. No `Co-Authored-By:` trailer, no
"Generated with …" footer, no AI or vendor attribution anywhere in a commit message or PR
body. Do not pass `--author`; do not change `git config`. CI rejects violations after the
fact, which is the expensive way to learn this. Enable the local hook once per clone:

```sh
git config core.hooksPath scripts/hooks
```

## Verify before you say you are done

From the repository root, the headless C++ suite:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target snapback_tests --parallel
ctest --test-dir build --output-on-failure
```

From `frontend/`: `npm run typecheck && npm run lint && npm run test && npm run build`.

The docs have guards too. Any backticked `file:symbol` citation must name a symbol that
exists (`scripts/check_doc_symbols.py`), and any path must exist
(`scripts/check_doc_paths.py`); run both after touching a doc. `scripts/check_dead_headers.py`
reads `git ls-files`, so stage new files before running it.

## Working style

- One focused commit per concern, with an imperative message that says why. Review the diff
  before committing. Never push unless the owner asks.
- Update the roadmap item you worked on in the same commit: what landed, what remains.
- Adding or renaming a native command touches `src/app/command_handlers.cpp`,
  `fixtures/ipc_commands.json` (and its count in `tests/test_ipc_contract.cpp`),
  `frontend/src/api.ts`, and `frontend/demo/backend.ts` together; the contract tests will
  tell you which one you forgot.
- Training and model tooling is developer-only by decision
  ([ADR-0006](docs/adr/0006-trainer-is-developer-tooling.md)); do not widen it without a new
  ADR.
