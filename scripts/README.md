# scripts/

| Script | Runs on | What it does |
|--------|---------|--------------|
| `verify.py` | any OS | Full local check. `guards` runs only the repo checks (also run in CI); `native`, `frontend-unit` and `frontend-component` run one test selection |
| `test_local.sh` | macOS, Linux | Headless C++ suite plus frontend typecheck/test/build |
| `test_local.ps1` | Windows | Same, plus optional `-IncludeWindowsDemo` |
| `run_benchmarks.sh` | macOS, Linux | Benchmark replay; `--hotpaths` for the micro-benchmarks |
| `run_benchmarks.ps1` | Windows | Benchmark replay |
| `windows_demo.ps1` | Windows | MSVC build, CTest, then launches `snapback.exe` |
| `gui_smoke_windows.ps1` | Windows | Launches the app and checks a real window appears |
| `gui_smoke_macos.sh` | macOS | Launches the app and requires a page-to-native round trip and a clean exit |
| `gui_acceptance.js` | Windows, macOS | Page-side script for the GUI smokes; calls through the real IPC bridge and reports a verdict |
| `gui_acceptance_cdp.mjs`, `gui_acceptance_cdp_bootstrap.js` | Windows | Drives the real React UI over WebView2's DevTools protocol |
| `package_windows.ps1` | Windows | CPack ZIP and NSIS installer, Authenticode signing |
| `validate_windows_package.ps1` | Windows | Checks an extracted package |
| `install_windows_package.ps1` | Windows | Installs a built package locally |

## Repo checks

All run from `verify.py guards`.

| Script | Checks |
|--------|--------|
| `check_doc_paths.py` | Paths and relative links in the docs point at files that exist |
| `check_dependency_pins.py` | Fetched C++ dependencies are pinned to a commit SHA or `URL_HASH` |
| `check_onnx_pins.py` | ONNX Runtime archives have a SHA-256 that's verified before extraction |
| `check_pin_freshness.py` | Pins vs. each project's latest release (`--offline` only parses) |
| `check_ps_exit_codes.py` | Every native command in a `.ps1` has its exit code checked |
| `check_coverage_exclusions.py` | Modules excluded from frontend coverage still have a unit test |
| `check_unit_test_wiring.py` | Every `frontend/tests/*.test.ts` is in the `test:unit` script |
| `check_no_bom.py` | No tracked file starts with a UTF-8 BOM |
| `check_dead_headers.py` | Every `src/**/*.hpp` is included by product code |
| `check_no_remote_subresources.py` | The frontend bundle loads nothing from the network |
| `check_release_tag.py` | A release tag matches the version in `CMakeLists.txt` |
| `check_release_legal.py` | `LICENSE`, `THIRD_PARTY_NOTICES.md` and the CPack wiring are present |

## .ps1 vs .sh

MSVC is a multi-config generator: the build type is picked at `--build` time and binaries land
in `build/Release/`. Make and Ninja pick it at configure time and put binaries in `build/`. The
shell scripts handle both layouts. [docs/running.md](../docs/running.md) has the raw `cmake`
and `ctest` commands if you'd rather skip the scripts.
