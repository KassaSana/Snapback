# Snapback E2E (WebDriver)

One real desktop happy path: launch the **built** app via `tauri-driver`, render the
dashboard, start a session, stop it, confirm the UI shows it completed.

This is the only test that exercises app boot, IPC wiring, asset embedding, and a
real window — the layers the Vitest component tests (which mock the Tauri
boundary) can't reach. See [`docs/TEST_BACKLOG.md`](../docs/TEST_BACKLOG.md) §6.

> ⚠️ **Status: scaffolded, not yet run green.** It was written to the Tauri
> WebDriver spec but has **not** been executed on a real desktop in this repo's
> environment. Treat the first run as bring-up: expect to adjust the binary path,
> driver versions, or waits. It is intentionally **not** part of the push CI gate
> (see `.github/workflows/e2e.yml`, `workflow_dispatch` only).

## Prerequisites

1. **Build the app** (embeds the frontend into a runnable binary):
   ```bash
   npm install            # repo root, first time
   npm run tauri build    # produces src-tauri/target/release/snapback(.exe)
   ```
2. **Install `tauri-driver`:**
   ```bash
   cargo install tauri-driver --locked
   ```
3. **Install the platform WebDriver** that `tauri-driver` proxies to:
   - **Windows:** `msedgedriver` matching your installed Edge version
     (`Get-AppxPackage *MicrosoftEdge*` for the version). Put it on `PATH`.
   - **Linux:** `webkit2gtk-driver` (provides `WebKitWebDriver`), e.g.
     `sudo apt-get install -y webkit2gtk-driver xvfb` (use `xvfb-run` if headless).
   - **macOS:** not supported by `tauri-driver` yet.

## Run

```bash
cd e2e
npm install
npm test
```

Overrides:
- `SNAPBACK_BIN` — absolute path to the built binary (if not the default release path).
- `TAURI_DRIVER` — path to the `tauri-driver` executable (default: `~/.cargo/bin`).

## What it checks

| Step | Assertion |
|------|-----------|
| Boot | `Session Control` heading renders |
| First run | dismiss the permission wizard if it appears |
| Start | typing a goal + `Start session` flips the status pill to `active` |
| Stop | `Stop session` flips the pill to `completed` |

## Notes

- The session **starts even without capture permissions** on a CI runner
  (warn-don't-block), so the happy path does not depend on real input capture.
- If the first-run wizard covers the screen, `dismissFirstRunWizard()` clicks
  "Skip for now" before interacting.
