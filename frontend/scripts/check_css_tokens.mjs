import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

/** Roadmap 10.10. Fails CI when styles.css references a custom property that is never
 *  defined — the bug that made --border, --card, and --text silently invalid. */

const here = dirname(fileURLToPath(import.meta.url));
const css = readFileSync(join(here, "../src/styles.css"), "utf8");

function checkCss(css) {
  // Comments cannot define tokens or introduce component colors.
  const source = css.replace(/\/\*[\s\S]*?\*\//g, (comment) => comment.replace(/[^\n]/g, " "));
  const defined = new Set();
  for (const match of source.matchAll(/(?:^|[;{])\s*(--[a-z0-9-]+)\s*:/gim)) {
    defined.add(match[1]);
  }

  const referenced = new Set();
  for (const match of source.matchAll(/var\(\s*(--[a-z0-9-]+)/g)) {
    referenced.add(match[1]);
  }

  const missing = [...referenced].filter((name) => !defined.has(name)).sort();
  assert.equal(
    missing.length,
    0,
    `Undefined CSS custom properties referenced in styles.css:\n${missing.join("\n")}`,
  );

  const tokenEnd = source.indexOf("\n*,");
  assert.ok(tokenEnd > 0, "Token definitions must precede the global box-sizing rule");
  const rawColors = source.slice(tokenEnd).match(/#[\da-f]{3,8}\b|rgba?\(|hsla?\(/gi) ?? [];
  assert.deepEqual(
    rawColors,
    [],
    "Raw colors outside the token-definition area: " + rawColors.join(", "),
  );

  // Full selector lists must be unique within their at-rule context. Responsive overrides
  // and shared base rules followed by individual variants remain valid.
  // Mask strings so braces inside content or attribute values do not affect nesting.
  const structural = source.replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g, (value) =>
    value.replace(/[^\n]/g, "_"),
  );
  const scopes = [];
  const seen = new Set();
  let start = 0;
  for (let index = 0; index < structural.length; index++) {
    const char = structural[index];
    if (char === "{") {
      const prelude = source.slice(start, index).trim().replace(/\s+/g, " ");
      const context = scopes.filter((scope) => scope.startsWith("@")).join(" / ");
      if (!prelude.startsWith("@")) {
        const selector = prelude
          .split(",")
          .map((part) => part.trim())
          .sort()
          .join(", ");
        const key = `${context} | ${selector}`;
        assert.ok(!seen.has(key), `Duplicate CSS selector: ${prelude} (${context || "global"})`);
        seen.add(key);
      }
      scopes.push(prelude);
      start = index + 1;
    } else if (char === "}") {
      scopes.pop();
      start = index + 1;
    }
  }

  return { referenced, defined };
}

// Regression fixtures exercise the guard without modifying the working stylesheet.
const fixture =
  ":root { --text: #111; }\n*, *::before, *::after { box-sizing: border-box; }\n.badge { color: var(--text); }";
assert.doesNotThrow(() => checkCss(fixture));
assert.doesNotThrow(() =>
  checkCss(fixture + "@media (max-width: 640px) { .badge { padding: 2px; } }"),
);
assert.throws(() => checkCss(fixture + ".other { color: #fff; }"), /Raw colors/);
assert.throws(() => checkCss(fixture + ".badge { padding: 2px; }"), /Duplicate CSS selector/);
assert.throws(() => checkCss(fixture + ".other { color: var(--missing); }"), /Undefined CSS/);
assert.throws(
  () => checkCss(fixture + "/* --commented: #fff; */ .other { color: var(--commented); }"),
  /Undefined CSS/,
);
const { referenced, defined } = checkCss(css);

console.log(`check_css_tokens: ${referenced.size} references, ${defined.size} definitions — ok`);
