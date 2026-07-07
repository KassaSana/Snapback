# Snapback Deployment

> **Note:** As of v0.2, Snapback ships as a **Tauri desktop app**. The Docker / Spring Boot deployment path has been removed.

## Prerequisites

- **Node.js** 18+ and npm
- **Rust** stable (`rustup` recommended)
- **Windows:** [WebView2](https://developer.microsoft.com/en-us/microsoft-edge/webview2/) (usually preinstalled on Windows 10/11). NSIS and WiX are pulled in by the Tauri CLI when bundling.
- **macOS:** Xcode command-line tools for native builds
- **Linux:** standard GTK/WebKit dev packages (see [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/))

## Desktop build

```bash
npm run setup
npm run tauri:build
```

Artifacts land in `src-tauri/target/release/bundle/`:

| Platform | Installer | Portable |
|----------|-----------|----------|
| Windows | `nsis/Snapback_*_x64-setup.exe` | `nsis/Snapback_*_x64-setup.exe` (installer) |
| Windows | `msi/Snapback_*_x64_en-US.msi` | — |
| macOS | `dmg/Snapback_*_x64.dmg` | `macos/Snapback.app` |
| Linux | `deb/`, `rpm/`, or `appimage/` | `appimage/` |

### Platform-specific installers

Build only the primary installer for your OS (faster than `targets: all`):

```bash
# Windows
npm run tauri:build:nsis

# macOS
npm run tauri:build:dmg
```

## ONNX training loop

1. **Export** from the app: Focus Feedback → Export training data.
2. **Train** (pick one):
   - **In-app:** set your Snapback repo path once, then **Train from export** (runs `ml.pipeline_cli --skip-export` via Python).
   - **CLI:** from repo root:
     ```bash
     python3 -m ml.pipeline_cli --output-dir "<app-data>/exports/training" --skip-export
     ```
   Produces `model.json`, `metrics.json`, and `model.onnx` when XGBoost trains successfully.
3. **Activate:** Focus Feedback → **Reload model** (auto-runs after successful in-app train).

Repo path is saved in app data (`training_repo.txt`) or set via `SNAPBACK_REPO`. Python needs training deps:

```bash
pip install -r ml/requirements-train.txt
```

Without `xgboost` / `onnxmltools`, training may fall back to a majority-classifier stub and skip ONNX export.

Dev and release builds use `--features onnx`. The header shows **Heuristic** until `model.onnx` is loaded.

## App icons

Desktop icons are generated from a square source image:

```bash
npm run icons:generate
```

This runs `tools/make-square-icon.mjs` (pads `app-icon.png` to 1024×1024) then `tauri icon app-icon-square.png`, populating `src-tauri/icons/`. Works on Windows and macOS.

Commit the generated icons after changing branding.

## Windows installer notes

- **NSIS** (`-setup.exe`) is the recommended Windows installer — per-user install, no admin required by default (`installMode: currentUser` in `tauri.conf.json`).
- **MSI** is also built when `targets` is `"all"`. Requires the **VBScript** optional Windows feature; enable via Settings → Apps → Optional features → More Windows features if `light.exe` fails.
- **WebView2:** the installer downloads and runs the WebView2 bootstrapper if the runtime is missing (`downloadBootstrapper`). Users need internet on first install unless you switch to `embedBootstrapper` or `offlineInstaller` in `tauri.conf.json`.
- **Permissions:** after install, users must allow input capture (see below). Document this in release notes.

## macOS permissions

Users must grant **Accessibility** and **Input Monitoring** before capture works. Document this in release notes.

## Windows permissions

Users may need to allow Snapback to capture keyboard/mouse input. If capture fails, the in-app health panel shows OS-specific setup steps (Settings → Privacy → Input / Accessibility, depending on version).

## Version bumps

Keep these in sync when releasing:

- `src-tauri/tauri.conf.json` → `version`
- `src-tauri/Cargo.toml` → `package.version`
- `package.json` → `version`

Then rebuild and tag the release.

## CI

`ci.yml`: Python tests, frontend test/typecheck/build, Rust check + test (ONNX on Ubuntu), Windows `cargo test`, feature parity.

Release bundles: `release.yml` on `v*` tags (Windows NSIS, macOS DMG). Build locally with `npm run tauri:build` if you need Linux artifacts.
