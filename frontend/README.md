# Snapback frontend

The React dashboard is plain web code (Vite + TypeScript + React). It speaks to
the native application through `src/bridge.ts`, which exposes `invoke(command,
args)` and event listeners over the injected `window.__snapback` bridge.

The command names in `src/app/commands.hpp` are the IPC contract. The bridge
maps each call to a `webview.bind()` command and delivers native events to the
registered listeners.

For development, run `npm run dev` and set `SNAPBACK_FRONTEND_URL` to the local
Vite URL. Production builds are inlined into the native app's self-contained
HTML bundle.
