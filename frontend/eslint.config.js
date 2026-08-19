// Roadmap 4.12. Flat config (ESLint 9+ format).
//
// Scope is deliberately narrow: correctness rules that catch real defects, plus the React
// Hooks rules, which are the ones this codebase can actually get wrong in a way tests miss --
// a dependency array that lies causes a stale closure, not a failing assertion.
//
// **Formatting is not linted here.** Prettier owns layout (`.prettierrc`), and the existing
// tree was deliberately not reformatted, so a style rule would report thousands of findings
// that nobody intends to act on and train everyone to ignore the output.
//
// Type-aware linting (`recommendedTypeChecked`) is not enabled. It needs a full program build
// per run, and `npm run typecheck` already runs the real compiler over the same files -- the
// marginal rules are not worth doubling the type-check cost.
import js from "@eslint/js";
import tseslint from "typescript-eslint";
import reactHooks from "eslint-plugin-react-hooks";

export default tseslint.config(
  {
    // Build output and coverage reports are generated; never lint them.
    ignores: ["dist/**", "coverage/**", "node_modules/**"],
  },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    // Build scripts are Node modules, not browser code. Without this, every `console` and
    // `process` in them reads as an undefined global.
    files: ["scripts/**/*.mjs"],
    languageOptions: {
      globals: { console: "readonly", process: "readonly", URL: "readonly" },
    },
  },
  {
    files: ["src/**/*.{ts,tsx}", "tests/**/*.{ts,tsx}", "scripts/**/*.mjs"],
    plugins: { "react-hooks": reactHooks },
    rules: {
      ...reactHooks.configs.recommended.rules,

      // TypeScript resolves identifiers itself and does it better -- it knows the DOM lib and
      // the module graph, which ESLint's global list does not. Leaving both on means every
      // browser API is a duplicate error in one tool and fine in the other.
      "no-undef": "off",

      // Warn, not error. Every one of the nine current reports is the same deliberate shape:
      //   useEffect(() => { void refresh(); }, [refresh])
      // where `refresh` is an async callback that awaits an IPC call before it setStates. The
      // rule flags it because it cannot see past the await boundary to prove the update is
      // not synchronous. The concern it exists for -- cascading synchronous re-renders -- is
      // real, so the reports stay visible rather than being switched off; they are just not a
      // reason to fail a build over a pattern the codebase uses uniformly and on purpose.
      "react-hooks/set-state-in-effect": "warn",

      // An unused variable is either a leftover or a typo. `_`-prefixed names are the
      // documented way to say "required by the signature, deliberately unused" -- the mock
      // handlers in tests/ rely on it.
      "@typescript-eslint/no-unused-vars": [
        "error",
        { argsIgnorePattern: "^_", varsIgnorePattern: "^_", caughtErrors: "none" },
      ],

      // `catch {}` with a comment explaining why the failure is survivable is a real and
      // frequent pattern here -- see useReviewWorkflow's delete flow. Empty *blocks*
      // elsewhere are still worth flagging.
      "no-empty": ["error", { allowEmptyCatch: true }],
    },
  },
  {
    // Tests build deliberately malformed payloads to prove the mappers survive them, which
    // means casting past the very types the mappers exist to enforce. `any` is the point
    // there, not an oversight.
    files: ["tests/**/*.{ts,tsx}"],
    rules: { "@typescript-eslint/no-explicit-any": "off" },
  },
);
