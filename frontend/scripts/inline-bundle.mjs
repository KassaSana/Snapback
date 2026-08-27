// Post-build step: fold the JS and CSS bundles into dist/index.html so the shipped page
// makes ZERO subresource requests.
//
// Why this exists: the desktop app loads the bundle over `file://`, and the vendored
// webview navigates with WKWebView's `loadRequest:` (webview.h:2376) rather than
// `loadFileURL:allowingReadAccessToURL:`. A `file://` page loaded that way is denied read
// access to its *sibling* files, so `./assets/index-*.js` never loaded and macOS showed a
// blank window — HTML and inline scripts ran fine, the module script failed with a bare
// resource error. Verified on 2026-07-25 with a probe page.
//
// A self-contained page sidesteps the whole class of problem and behaves identically on
// WebView2 and WebKitGTK. A future custom URL scheme handler could give the page a real
// origin; see the roadmap.
//
// CSP: inlining would be blocked by `script-src 'self'`, so we compute the script's
// sha256 and add it to the policy. Hashes authorize one exact script and nothing else, so
// the policy stays as strict as before — notably we do NOT add 'unsafe-inline'.

import { createHash } from "node:crypto";
import { readFileSync, writeFileSync, existsSync, rmSync } from "node:fs";
import { dirname, join, resolve } from "node:path";
import { fileURLToPath } from "node:url";

// Usage: inline-bundle.mjs [distDir] [htmlFile] [outputName]
//
// Defaults reproduce the desktop build exactly (`dist/index.html`, inlined in place). The
// arguments exist for the hosted demo, which builds `demo.html` into `dist-demo/` and needs it
// published as `index.html` so a static host serves it as the directory index.
const frontendDir = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const distDir = join(frontendDir, process.argv[2] ?? "dist");
const htmlFile = process.argv[3] ?? "index.html";
const outputName = process.argv[4] ?? htmlFile;
const indexPath = join(distDir, htmlFile);

if (!existsSync(indexPath)) {
  console.error(`inline-bundle: ${indexPath} not found — run \`vite build\` first`);
  process.exit(1);
}

let html = readFileSync(indexPath, "utf8");
const inlinedScripts = [];

// <script type="module" crossorigin src="./assets/index-XXXX.js"></script>
html = html.replace(/<script\b[^>]*\bsrc="\.\/([^"]+)"[^>]*><\/script>/g, (_match, assetPath) => {
  const code = readFileSync(join(distDir, assetPath), "utf8");
  inlinedScripts.push(code);
  return `<script type="module">${code}</script>`;
});

// <link rel="stylesheet" crossorigin href="./assets/index-XXXX.css">
html = html.replace(
  /<link\b[^>]*\brel="stylesheet"[^>]*\bhref="\.\/([^"]+)"[^>]*>/g,
  (_match, assetPath) => `<style>${readFileSync(join(distDir, assetPath), "utf8")}</style>`,
);

if (inlinedScripts.length === 0) {
  console.error("inline-bundle: no local <script src> found — did the build output change?");
  process.exit(1);
}

// Authorize exactly the scripts we just inlined, by hash.
const hashes = inlinedScripts
  .map((code) => `'sha256-${createHash("sha256").update(code, "utf8").digest("base64")}'`)
  .join(" ");
html = html.replace("script-src 'self'", `script-src 'self' ${hashes}`);

// Fail the build rather than shipping a page that silently cannot load itself. Any
// remaining local src/href is a subresource the desktop webview will be denied.
const leftovers = [...html.matchAll(/\b(?:src|href)="(\.\/[^"]+|\/[^"/][^"]*)"/g)].map((m) => m[1]);
if (leftovers.length > 0) {
  console.error(`inline-bundle: unresolved local references remain: ${leftovers.join(", ")}`);
  process.exit(1);
}
if (!html.includes("sha256-")) {
  console.error("inline-bundle: CSP was not updated with a script hash");
  process.exit(1);
}

const outputPath = join(distDir, outputName);
writeFileSync(outputPath, html);
if (outputPath !== indexPath) {
  // The source page is now a duplicate that no longer matches its own inlined hash if it were
  // edited. Removing it leaves exactly one page to publish.
  rmSync(indexPath);
}
console.log(
  `inline-bundle: ${outputName} is self-contained ` +
    `(${inlinedScripts.length} script(s), ${(html.length / 1024).toFixed(0)} kB, CSP hash-pinned)`,
);
