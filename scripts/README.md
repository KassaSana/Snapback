# scripts/

**Which of these run on your machine.** Added 2026-07-23 for Roadmap 12.5, because the
development host is macOS and seven of the eight files here were PowerShell — so the
documented way to run the test suite did not run on the machine it was documented for.

| Script | Runs on | What it does |
|--------|---------|--------------|
| `test_local.sh` | macOS, Linux | Headless C++ suite + frontend typecheck/test/build |
| `test_local.ps1` | Windows | Same, plus optional `-IncludeWindowsDemo` |
| `run_benchmarks.sh` | macOS, Linux | Benchmark replay; `--hotpaths` for the micro-benchmarks |
| `run_benchmarks.ps1` | Windows | Same, replay only |
| `check_doc_paths.py` | anywhere | Fails if a doc names a file that does not exist (runs in CI) |
| `run_feature_parity_dual.py` | anywhere | Rust-vs-C++ feature vector diff; needs the Rust tree |
| `windows_demo.ps1` | **Windows only** | MSVC build + CTest + launches `snapback.exe` |
| `gui_smoke_windows.ps1` | **Windows only** | Verifies a real Snapback window appears |
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

If you skip the scripts entirely, the raw `cmake`/`ctest` invocations are in
[README.md](../README.md) and [docs/testing_strategy.md](../docs/testing_strategy.md).
