# Frontend: reuse ../Snapback/frontend unchanged

The React dashboard does **not** get rewritten. It's plain web code (Vite +
TypeScript + React) and knows nothing about Rust vs C++ — it only speaks the IPC
protocol: `invoke("command_name", args)` and event listeners.

Two compatibility rules keep it working against the C++ core:

1. **Command names match.** Every name in `src/app/commands.hpp` `register_commands`
   must equal a name the frontend calls via `invoke(...)`, which equals a name in the
   Rust `tauri::generate_handler![...]` list. That three-way match is the contract.

2. **The `invoke` shim + event bus.** Tauri injects `window.__TAURI__` and the
   `@tauri-apps/api` `invoke`/`listen` helpers. `webview/webview` does not. So the
   C++ host injects a tiny shim (via `webview.init(...)`) that:
   - maps `invoke(name, args)` onto the bound C++ functions, and
   - exposes `window.__snapback.emit(event, payload)` for the host to push events
     (the `emit()` helper in `commands.hpp` calls it).

   If the frontend imports `@tauri-apps/api` directly, add a thin adapter module so
   those imports resolve to the shim instead.

## During the port

- **Dev:** run the existing Vite dev server (`cd ../Snapback/frontend && npm run dev`)
  and point `main.cpp`'s `w.navigate(...)` at `http://localhost:5173`.
- **Prod:** `npm run build` there, then embed `dist/` in the C++ binary and load it
  via a custom scheme or data URLs (CSP-safe, offline).
