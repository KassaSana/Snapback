import assert from "node:assert/strict";

import { MemoryStorage, hasWorkingStorage, installMemoryStorage } from "./memoryStorage";

// Roadmap 11.11. The shim itself needs a test, and it cannot be the component suite's — the
// component suite is the thing that depends on it. This runs under the `tsx` runner, which
// works on any Node, so the fallback is verified even on the runtime that made it necessary.

// Spec-shaped enough that a test cannot tell it from the browser's.
{
  const storage = new MemoryStorage();
  assert.equal(storage.length, 0);
  assert.equal(storage.getItem("missing"), null); // null, never undefined

  storage.setItem("a", "1");
  storage.setItem("b", "2");
  assert.equal(storage.length, 2);
  assert.equal(storage.getItem("a"), "1");

  // Insertion order, as the spec requires of key().
  assert.equal(storage.key(0), "a");
  assert.equal(storage.key(1), "b");
  assert.equal(storage.key(2), null);
  assert.equal(storage.key(-1), null);

  storage.removeItem("a");
  assert.equal(storage.getItem("a"), null);
  assert.equal(storage.length, 1);

  storage.clear();
  assert.equal(storage.length, 0);
  assert.equal(storage.getItem("b"), null);
}

// Values and keys are coerced to strings, so a test that stores a boolean reads back the same
// thing a browser would give it.
{
  const storage = new MemoryStorage();
  storage.setItem("flag", true as unknown as string);
  assert.equal(storage.getItem("flag"), "true");
  storage.setItem(7 as unknown as string, "seven");
  assert.equal(storage.getItem("7"), "seven");
}

// Detection: a working Storage is left alone, and anything unusable is replaced.
{
  const working = { localStorage: new MemoryStorage() };
  assert.equal(hasWorkingStorage(working), true);

  assert.equal(hasWorkingStorage({}), false);
  assert.equal(hasWorkingStorage({ localStorage: undefined }), false);
  assert.equal(hasWorkingStorage(null), false);
  assert.equal(hasWorkingStorage(undefined), false);

  // Node 26's flag-gated global is the case that started this: present in some shape, but not
  // usable. A Storage that throws on use is not a Storage.
  const throwing = {
    localStorage: {
      setItem() {
        throw new Error("localStorage is not available");
      },
    },
  };
  assert.equal(hasWorkingStorage(throwing), false);
}

// Install replaces only what needs replacing, and says whether it did.
{
  const broken: Record<string, unknown> = {};
  assert.equal(installMemoryStorage([broken]), true);
  assert.equal(hasWorkingStorage(broken), true);

  const existing = new MemoryStorage();
  existing.setItem("keep", "me");
  const healthy = { localStorage: existing };
  assert.equal(installMemoryStorage([healthy]), false);
  // Untouched: a working environment must not have its Storage swapped out from under it,
  // or CI would stop testing what it thinks it tests.
  assert.equal(healthy.localStorage, existing);
  assert.equal(healthy.localStorage.getItem("keep"), "me");

  // Nulls in the target list are skipped rather than throwing — `window` does not exist in
  // every runner.
  assert.equal(installMemoryStorage([null, undefined]), false);
}

console.log("memoryStorage.test.ts passed");
