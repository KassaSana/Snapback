import { defineConfig } from "vitest/config";
import react from "@vitejs/plugin-react";

const host = process.env.TAURI_DEV_HOST;

export default defineConfig({
  plugins: [react()],
  base: "./",
  // Component/integration tests only. The lightweight `tsx tests/*.test.ts`
  // scripts are intentionally excluded (they're pure-logic, no DOM), so vitest
  // owns `.test.tsx` and the tsx runner owns `.test.ts`.
  test: {
    environment: "jsdom",
    include: ["tests/**/*.test.tsx"],
    setupFiles: ["tests/setup.ts"],
    coverage: {
      // Report-only for now (no thresholds). NOTE: this measures the vitest
      // component suite only. Pure-logic modules (insightsMetrics, healthPoll,
      // permissionWizardState, apiMappers, utils, …) are covered by the
      // `tsx tests/*.test.ts` scripts, so their numbers here read artificially
      // low. See docs/TEST_BACKLOG.md #5.
      provider: "v8",
      reporter: ["text", "html"],
      include: ["src/**/*.{ts,tsx}"],
      exclude: ["src/main.tsx", "src/vite-env.d.ts"],
    },
  },
  clearScreen: false,
  server: {
    port: 5173,
    strictPort: true,
    host: host || false,
    hmr: host
      ? {
          protocol: "ws",
          host,
          port: 1421,
        }
      : undefined,
    watch: {
      ignored: ["**/src-tauri/**"],
    },
  },
  envPrefix: ["VITE_", "TAURI_"],
  build: {
    target: process.env.TAURI_PLATFORM === "windows" ? "chrome105" : "safari13",
    minify: !process.env.TAURI_DEBUG ? "esbuild" : false,
    sourcemap: !!process.env.TAURI_DEBUG,
  },
});
