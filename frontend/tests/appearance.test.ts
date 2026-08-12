import assert from "node:assert/strict";

import {
  applyAppearance,
  APPEARANCE_ATTRIBUTE,
  readAppearanceMode,
  resolvedColorScheme,
  writeAppearanceMode,
} from "../src/appearance";

const storage = new Map<string, string>();
(globalThis as { localStorage?: Storage }).localStorage = {
  getItem: (key) => storage.get(key) ?? null,
  setItem: (key, value) => {
    storage.set(key, value);
  },
  removeItem: (key) => {
    storage.delete(key);
  },
  clear: () => storage.clear(),
  key: () => null,
  length: 0,
};

writeAppearanceMode("dark");
assert.equal(readAppearanceMode(), "dark");
assert.equal(resolvedColorScheme("dark"), "dark");
assert.equal(resolvedColorScheme("light"), "light");

const root = { style: { colorScheme: "" }, attributes: new Map<string, string>() } as unknown as HTMLElement;
Object.defineProperty(root, "setAttribute", {
  value(name: string, value: string) {
    (root as unknown as { attributes: Map<string, string> }).attributes.set(name, value);
  },
});
Object.defineProperty(root, "getAttribute", {
  value(name: string) {
    return (root as unknown as { attributes: Map<string, string> }).attributes.get(name) ?? null;
  },
});

applyAppearance("system", root);
assert.equal(root.getAttribute(APPEARANCE_ATTRIBUTE), "system");

console.log("appearance.test.ts — ok");
