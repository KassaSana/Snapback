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
      // This measures the Vitest component suite. Pure-logic modules also have
      // lightweight `tsx tests/*.test.ts` coverage, but those scripts do not feed V8's
      // aggregate. Floors are rounded down from the 2026-07-26 measured baseline so CI
      // catches regressions without pretending the component-only number is total coverage.
      provider: "v8",
      reporter: ["text", "html"],
      include: ["src/**/*.{ts,tsx}"],
      exclude: [
        "src/main.tsx",
        "src/vite-env.d.ts",
        // Roadmap 11.11. Pure-logic modules whose branches are exercised by the `tsx
        // tests/*.test.ts` runner, which does not feed V8's aggregate. Counting them here
        // measured them as near-uncovered while they are, in fact, the most directly tested
        // code in the tree — which made the component number mean less, not more.
        //
        // This list is not a way to opt out of testing: `scripts/check_coverage_exclusions.py`
        // fails the build unless every entry has a matching `tests/<name>.test.ts` that
        // `npm run test:unit` actually runs. Adding a file here without a test is a red build.
        "src/sessionStatus.ts",
        "src/idleThreshold.ts",
        "src/analyticsChart.ts",
        "src/activityDeletion.ts",
        "src/myDataExport.ts",
        "src/focusStreak.ts",
      ],
      thresholds: {
        statements: 76,
        branches: 66,
        functions: 74,
        lines: 77,
      },
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
