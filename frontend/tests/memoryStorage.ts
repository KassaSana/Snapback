// Roadmap 11.11. A Web Storage implementation for test environments that do not supply one.
//
// **The cause was not jsdom.** `vite.config.ts` sets `environment: "jsdom"`, the lockfile and
// installed trees agree, and the document URL is a real origin (`http://localhost:3000/`), so
// jsdom is willing to provide `localStorage`. What actually happens on Node 26 is:
//
//     ExperimentalWarning: localStorage is not available because --localstorage-file
//     was not provided.
//
// Node 26 ships its **own** experimental global `localStorage`, gated behind a flag nobody
// passes. It exists on `globalThis` before the jsdom environment installs its window
// properties, so jsdom's implementation never wins and every read lands on Node's unavailable
// one. Node 22 — which CI pins — has no such global, which is exactly why the suite was green
// there and 47 of 87 cases failed here.
//
// So this is deliberately **not** a workaround for a broken DOM. It fills a gap created by a
// host global that shadows the environment's, and it installs itself only when the environment
// did not end up with a working Storage. On CI's Node 22 nothing here runs, and jsdom's real
// implementation is used untouched.
//
// Lives under `tests/` because it is test scaffolding and must never reach the bundle.

/** The subset of the Web Storage API the app and its tests use, implemented in memory. */
export class MemoryStorage implements Storage {
  #entries = new Map<string, string>();

  get length(): number {
    return this.#entries.size;
  }

  key(index: number): string | null {
    // Insertion order, which is what the spec requires of a Storage object's key ordering.
    if (!Number.isInteger(index) || index < 0) return null;
    return [...this.#entries.keys()][index] ?? null;
  }

  getItem(key: string): string | null {
    // `null` for a missing key, never `undefined`: callers branch on `=== null`, and a
    // Storage that returns undefined would pass a truthiness check and fail a strict one.
    return this.#entries.has(String(key)) ? (this.#entries.get(String(key)) as string) : null;
  }

  setItem(key: string, value: string): void {
    // Both are coerced to strings, as the spec requires. A test that stores `true` and reads
    // back `"true"` is testing the same thing the browser does.
    this.#entries.set(String(key), String(value));
  }

  removeItem(key: string): void {
    this.#entries.delete(String(key));
  }

  clear(): void {
    this.#entries.clear();
  }
}

/** True when `target` already has a Storage that can actually be read and written. */
export function hasWorkingStorage(target: unknown): boolean {
  const candidate = (target as { localStorage?: unknown } | null | undefined)?.localStorage;
  if (!candidate || typeof candidate !== "object") return false;
  try {
    const storage = candidate as Storage;
    const probe = "__snapback_probe__";
    storage.setItem(probe, "1");
    const readable = storage.getItem(probe) === "1";
    storage.removeItem(probe);
    return readable;
  } catch {
    // A Storage that throws on use is not a Storage. Node's flag-gated one can behave this
    // way, and so can a browser with storage disabled.
    return false;
  }
}

/**
 * Install `MemoryStorage` on every given target that lacks a working one.
 *
 * Returns true if anything was installed, so a caller can report that it stepped in rather
 * than leaving the difference between environments silent.
 */
export function installMemoryStorage(targets: unknown[]): boolean {
  let installed = false;
  for (const target of targets) {
    if (!target || hasWorkingStorage(target)) continue;
    Object.defineProperty(target, "localStorage", {
      value: new MemoryStorage(),
      configurable: true,
      writable: true,
    });
    installed = true;
  }
  return installed;
}
