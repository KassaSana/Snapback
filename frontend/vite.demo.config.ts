// Build config for the hosted demo only. The desktop bundle is built by `vite.config.ts`.
//
// Two things differ, and both matter:
//
//   - The entry is `demo.html`, not `index.html`. That is what guarantees the demo bridge and
//     its invented dataset cannot reach the shipped desktop bundle: they are not in its graph.
//   - The output goes to `dist-demo/`, so a demo build never overwrites the `dist/` directory
//     that CMake copies next to `snapback.exe`. Building the demo must not be able to ship
//     sample data inside the real app.
//
// `base: "./"` is inherited in spirit but restated here: the published page is a single
// self-contained file after `inline-bundle.mjs`, so it works at any path a static host picks.

import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

export default defineConfig({
  plugins: [react()],
  base: "./",
  build: {
    target: "safari13",
    minify: "esbuild",
    sourcemap: false,
    outDir: "dist-demo",
    emptyOutDir: true,
    rollupOptions: {
      input: "demo.html",
    },
  },
});
