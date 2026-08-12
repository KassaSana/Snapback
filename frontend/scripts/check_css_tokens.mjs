import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

/** Roadmap 10.10. Fails CI when styles.css references a custom property that is never
 *  defined — the bug that made --border, --card, and --text silently invalid. */

const here = dirname(fileURLToPath(import.meta.url));
const css = readFileSync(join(here, "../src/styles.css"), "utf8");

const defined = new Set();
for (const match of css.matchAll(/^\s*(--[a-z0-9-]+)\s*:/gim)) {
  defined.add(match[1]);
}

const referenced = new Set();
for (const match of css.matchAll(/var\(\s*(--[a-z0-9-]+)/g)) {
  referenced.add(match[1]);
}

const missing = [...referenced].filter((name) => !defined.has(name)).sort();
assert.equal(
  missing.length,
  0,
  `Undefined CSS custom properties referenced in styles.css:\n${missing.join("\n")}`,
);

console.log(`check_css_tokens: ${referenced.size} references, ${defined.size} definitions — ok`);
