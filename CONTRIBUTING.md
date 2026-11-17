# Contributing

## Build and test

```text
python scripts/verify.py
```

That builds and tests the headless C++ core, lints and tests the frontend, and runs the repo
checks in `scripts/`. [docs/running.md](docs/running.md) has per-platform setup and how to
launch the desktop app.

To run one thing:

```text
python scripts/verify.py native <ctest regex>
python scripts/verify.py frontend-unit <file>.test.ts
python scripts/verify.py frontend-component <file>.test.tsx
```

## Things that are easy to miss

- **Frontend unit tests are listed by hand.** A new `frontend/tests/*.test.ts` has to be added
  to the `test:unit` script in `frontend/package.json`, or it never runs.
- **IPC changes touch four places:** the handler in `src/app/command_handlers.cpp`, the
  registry, `fixtures/ipc_commands.json`, and the wrapper in `frontend/src/api.ts`.
  `tests/test_ipc_contract.cpp` fails if they disagree.
- **Two JSON conventions.** IPC payloads use camelCase keys and snake_case command names.
  Training/fixture data uses snake_case keys. See [ARCHITECTURE.md](docs/ARCHITECTURE.md).
- **C++ dependencies are pinned to a commit SHA or a hash**, never a tag. See
  [dependencies.md](docs/dependencies.md).
- Cite code in docs as `file:symbol`, not by line number.

## Style

`.clang-format`, `frontend/eslint.config.js` and `frontend/prettier.config.js` describe the
house style. Format new files; don't reformat existing ones inside a behaviour change.
`npm run lint` has to pass.

Comments explain why, and stay short. History goes in commit messages.

Design decisions are recorded in [docs/adr/](docs/adr/README.md).
