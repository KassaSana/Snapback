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

## The hosted demo

`frontend/demo/` is a second entry point that publishes this same dashboard as a static page,
so the project can be shown without installing anything.

**It is a separate entry, not a build flag.** `npm run build:demo` builds `demo.html` into
`dist-demo/`; the desktop build still builds `index.html` into `dist/`. The demo bridge and its
invented dataset are not in the desktop bundle's module graph at all, so nothing about keeping
sample data out of the shipped app depends on an environment variable being set correctly, on
dead-code elimination firing, or on a reviewer noticing. The two outputs also go to different
directories, so a demo build cannot overwrite the `dist/` that CMake copies beside
`snapback.exe`.

`demo/bridge.ts` installs a `window.__snapback` that answers the same commands the C++ host
does, backed by `demo/backend.ts` over a generated dataset. Two constraints it holds to:

- Aggregates are computed from the same generated rows the timeline shows, so Now and Review
  cannot disagree — the failure 10.11 and 10.13 were opened for.
- Anything that would write, read, or open a real file reports that it is unavailable rather
  than inventing a path.

A browser tab cannot read the active window or input idle time from the operating system, so
the demo can never be the real product. The page says so in a banner rather than leaving a
visitor to assume the numbers are measurements.
