# scripts/

Use the platform column before running a script; the PowerShell packaging/demo tools depend
on Windows tooling, while the shell wrappers cover macOS and Linux.

| Script | Runs on | What it does |
|--------|---------|--------------|
| `test_local.sh` | macOS, Linux | Headless C++ suite + frontend typecheck/test/build |
| `test_local.ps1` | Windows | Same, plus optional `-IncludeWindowsDemo` |
| `run_benchmarks.sh` | macOS, Linux | Benchmark replay; `--hotpaths` for the micro-benchmarks |
| `run_benchmarks.ps1` | Windows | Same, replay only |
| `check_doc_paths.py` | any OS | verifies that documented repository paths exist |
| `check_doc_symbols.py` | any OS | verifies a <file>:<symbol> citation still names something in that file, and rejects a bare <file>:<line> number |
| `check_dependency_pins.py` | any OS | verifies every fetched C++ dependency is pinned to a commit SHA or a `URL_HASH` — see [docs/dependencies.md](../docs/dependencies.md) |
| `check_onnx_pins.py` | any OS | verifies the vendored ONNX Runtime archives carry a SHA-256 that is checked before extraction |
| `check_pin_freshness.py` | any OS | compares those pins plus ONNX Runtime to each project's latest GitHub release; `--offline` only parses |
| `check_ps_exit_codes.py` | any OS | verifies every native call in a `.ps1` is exit-code checked, since `$ErrorActionPreference` does not cover them |
| `check_commit_attribution.py` | any OS | walks every ref and rejects AI/vendor attribution trailers and unrecognised author identities — see `hooks/commit-msg` below |
| `check_coverage_exclusions.py` | any OS | verifies every module excluded from the frontend coverage gate has a test that `npm run test:unit` actually runs |
| `check_unit_test_wiring.py` | any OS | the converse: verifies every `frontend/tests/*.test.ts` appears in the hand-chained `test:unit` script, so a new test file cannot be silently unrun |
| `check_no_bom.py` | any OS | verifies no tracked file starts with a UTF-8 byte-order mark |
| `check_no_remote_subresources.py` | any OS | verifies the built frontend bundle fetches nothing at runtime — no CDN, font, or remote script |
| `check_release_tag.py` | any OS | verifies a release tag names exactly the `project(... VERSION x.y.z)` in `CMakeLists.txt` |
| `check_scripts_documented.py` | any OS | verifies every script in this directory is named in this table — the guard that keeps it honest |
| `test_commit_msg_hook.sh` | macOS, Linux | runs the real `hooks/commit-msg` against real messages; each forbidden pattern has a case that fails when only that pattern is removed |
| `windows_demo.ps1` | **Windows only** | MSVC build + CTest + launches `snapback.exe` |
| `gui_smoke_windows.ps1` | **Windows only** | Verifies a real Snapback window appears |
| `gui_smoke_macos.sh` | **macOS only** | Launches the app and requires a session round trip, a loaded bundle, and a clean run-loop exit |
| `package_windows.ps1` | **Windows only** | CPack ZIP, IExpress installer, Authenticode signing |
| `validate_windows_package.ps1` | **Windows only** | Checks an extracted package |
| `install_windows_package.ps1` | **Windows only** | Installs a built package locally |

The Windows-only five are not portable in principle, not just unported: they drive MSVC,
`signtool`, IExpress, and the packaged `snapback.exe`. Off Windows they are exercised
only by CI.

## The `.ps1` / `.sh` difference that matters

MSVC is a **multi-config** generator: the build type is chosen at `--build` time and
binaries land in `build/Release/`. Make and Ninja are **single-config**: the build type
must be set at *configure* time with `-DCMAKE_BUILD_TYPE=`, and binaries land directly in
`build/`. The shell scripts pass both and probe both output layouts, which is why they are
ports rather than translations.

If you skip the scripts entirely, [docs/running.md](../docs/running.md) has the raw
`cmake`/`ctest` invocations per OS, plus what can and cannot be built on each host.

## `hooks/commit-msg`

`check_commit_attribution.py` rejects AI attribution trailers in CI, across every ref. That
is the right place for a gate but the wrong place for a *fix*: by the time CI runs, the
commit exists, and removing a trailer from it means rewriting history — which changes every
downstream SHA and breaks the release tags and CI-conclusion checks the release gate reads.
`hooks/commit-msg` deletes the trailers while they can still be deleted for free.

Hooks are not version controlled, so enable it once per clone:

```
git config core.hooksPath scripts/hooks
```

On macOS and Linux the hook must also be executable; git records that bit, so
`git update-index --chmod=+x scripts/hooks/commit-msg` fixes a clone that lost it.

The hook strips trailer-shaped boilerplate silently — `Co-authored-by:`, `Generated with`,
anything naming `cursoragent@cursor.com` — then re-checks what survived by calling
`check_commit_attribution.py --message-file`, so the hook and the CI gate share one
definition of attribution rather than drifting apart. An authorship claim written into
ordinary prose is reported and the commit is refused, not silently reworded: that edit is
the author's call. Prose that merely *names* a tool is left alone.

Two things it cannot do. Cursor's cloud and background agents commit under
`Cursor Agent <cursoragent@cursor.com>` as the **author**, server-side, where no local hook
runs; and any clone that skips the `core.hooksPath` line above is unprotected. CI remains
the backstop for both.
