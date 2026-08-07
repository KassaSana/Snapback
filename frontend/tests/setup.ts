// Vitest setup for component tests: register jest-dom matchers
// (toBeInTheDocument, etc.) on vitest's expect.
import "@testing-library/jest-dom/vitest";

import { installMemoryStorage } from "./memoryStorage";

// Roadmap 11.11. Node 26 ships an experimental global `localStorage` that is unavailable
// without `--localstorage-file`, and it shadows the one jsdom would otherwise provide — which
// is why 47 of 87 component cases failed on this machine and passed on CI's Node 22. See
// memoryStorage.ts for the full diagnosis.
//
// This is a no-op wherever the environment already supplies a working Storage, so CI's
// behaviour is unchanged and the suite stops depending on which Node happens to be installed.
// Both targets are patched because `window` and `globalThis` are the same object under jsdom
// but not under every runner, and code reaches for either.
if (installMemoryStorage([globalThis, typeof window === "undefined" ? null : window])) {
  // Said out loud rather than fixed silently: a difference between this run and CI's should
  // be visible in the log, not discovered later as a behavioural surprise.
  console.info(
    "[test setup] This runtime provides no usable localStorage; installed an in-memory one (Roadmap 11.11).",
  );
}
