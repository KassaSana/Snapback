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
npm install
cd frontend && npm install && cd ..
npm run tauri:build
```

Artifacts land in `src-tauri/target/release/bundle/`:

| Platform | Installer | Portable |
|----------|-----------|----------|
| Windows | `nsis/Snapback_*_x64-setup.exe` | `nsis/Snapback_*_x64-setup.exe` (installer) |
| Windows | `msi/Snapback_*_x64_en-US.msi` | — |
| macOS | `dmg/Snapback_*_x64.dmg` | `macos/Snapback.app` |
| Linux | `deb/`, `rpm/`, or `appimage/` | `appimage/` |

### Faster Windows iteration

Build only the NSIS installer (skips MSI/WiX):

```bash
npm run tauri:build:nsis
```

## App icons

Desktop icons are generated from a square source image:

```bash
npm run icons:generate
```

This runs `tools/make-square-icon.ps1` (pads `app-icon.png` to 1024×1024) then `npx tauri icon app-icon-square.png`, populating `src-tauri/icons/`.

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

See `.github/workflows/ci.yml` — Python training tests, frontend checks, and `cargo check` / `cargo test`.

Full release bundles are built locally or via a release workflow (not yet automated in CI).
