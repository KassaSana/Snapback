// Installs the `window.__snapback` object the React app expects.
//
// In the desktop app this object is injected by the C++ host through the webview's `bind()`
// (see `src/app/ipc_shim.cpp`). Nothing in the browser provides it, which is why
// `src/bridge.ts` throws "Snapback bridge is unavailable" if you simply open the built page.
// This file is the demo's stand-in for that host.

import { DemoBackend } from "./backend";

type Json = Record<string, unknown>;
type Listener = (event: { payload: unknown }) => void;

/** Roughly the latency of a real IPC round trip, so loading states are actually visible. */
const LATENCY_MS = 45;

/** How often the live session advances. Fast enough to feel alive, slow enough to read. */
const TICK_MS = 5000;

export function installDemoBridge(): void {
  const backend = new DemoBackend(Date.now());
  const listeners = new Map<string, Set<Listener>>();

  const emit = (event: string, payload: unknown) => {
    for (const listener of listeners.get(event) ?? []) {
      listener({ payload });
    }
  };

  window.__snapback = {
    invoke<T>(command: string, args?: Record<string, unknown>): Promise<T> {
      return new Promise<T>((resolve, reject) => {
        window.setTimeout(() => {
          try {
            resolve(backend.handle(command, (args ?? {}) as Json) as T);
          } catch (error) {
            reject(error instanceof Error ? error : new Error(String(error)));
          }
        }, LATENCY_MS);
      });
    },
    listen<T>(event: string, handler: (event: { payload: T }) => void): Promise<() => void> {
      const set = listeners.get(event) ?? new Set<Listener>();
      set.add(handler as Listener);
      listeners.set(event, set);
      return Promise.resolve(() => {
        set.delete(handler as Listener);
      });
    },
  };

  window.setInterval(() => {
    const prediction = backend.tick();
    if (prediction) emit("prediction", prediction);
  }, TICK_MS);
}
