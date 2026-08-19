# Snapback frontend

The React dashboard is plain web code (Vite + TypeScript + React). It speaks to
the native application through `frontend/src/bridge.ts`, which exposes `invoke(command,
args)` and event listeners over the injected `window.__snapback` bridge.

The command names in `src/app/commands.hpp` are the IPC contract. The bridge
maps each call to a `webview.bind()` command and delivers native events to the
registered listeners.

Paths here are repo-relative, like every other doc — `src/` is the C++ tree and
`frontend/src/` is this one. They were not always: this file used to drop the `frontend/`
prefix when naming its own modules, one line above a `src/app/commands.hpp` that did mean
the C++ tree, so the same prefix named two different directories in adjacent sentences.

For development, run `npm run dev` and set `SNAPBACK_FRONTEND_URL` to the local Vite URL.
`npm run build` emits a hash-pinned, self-contained `frontend/dist/index.html`; the native
build copies that frontend directory beside the executable.

The dashboard reads no build-time environment variable. It has no HTTP backend and never
had one — everything crosses the `window.__snapback` bridge.
