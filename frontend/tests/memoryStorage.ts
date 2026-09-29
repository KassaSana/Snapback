// A Web Storage implementation for runtimes where localStorage is unusable. On Node 26 a
// flag-gated experimental global `localStorage` shadows jsdom's; this installs only when the
// environment ended up without a working Storage (never on CI's Node 22). Test-only.

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
