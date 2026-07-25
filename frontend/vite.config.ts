import { defineConfig } from "vitest/config";
import react from "@vitejs/plugin-react";

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
    host: false,
  },
  envPrefix: ["VITE_"],
  build: {
    target: "safari13",
    minify: "esbuild",
    sourcemap: false,
  },
});
