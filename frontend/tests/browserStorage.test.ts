import assert from "node:assert/strict";
import { readdirSync, readFileSync } from "node:fs";
import { join } from "node:path";

import {
  ACTIVITY_STORAGE_KEYS,
  BROWSER_STORAGE_KEYS,
  clearActivityStorage,
  SESSION_PRESETS_KEY,
  APPEARANCE_STORAGE_KEY,
  FIRST_RUN_ACK_KEY,
  ONBOARDING_DONE_KEY,
  REVIEW_RANGE_STORAGE_KEY,
  WORK_APP_TEACH_DONE_KEY,
} from "../src/browserStorage";
import { MemoryStorage } from "./memoryStorage";

// --- The registry is complete ------------------------------------------------------------
//
// Roadmap 8.15. The defect was a key nobody had classified, which therefore survived an erase
// that claimed to remove every app-owned copy of the user's activity. A list is only worth
// having if it cannot fall behind the code, so this walks `src/` and fails on any `snapback.*`
// literal without a row. That is what makes "the next key added has to pick a side" true
// rather than a hope.
{
  const srcDir = join(import.meta.dirname, "..", "src");
  const found = new Set<string>();
  for (const name of readdirSync(srcDir, { recursive: true }) as string[]) {
    if (!name.endsWith(".ts") && !name.endsWith(".tsx")) continue;
    const source = readFileSync(join(srcDir, name), "utf8");
    for (const match of source.matchAll(/"(snapback\.[A-Za-z0-9_.]+)"/g)) {
      found.add(match[1]);
    }
  }
  assert.ok(found.size > 0, "the scan found no keys at all, so it proves nothing");

  const classified = new Set(BROWSER_STORAGE_KEYS.map((entry) => entry.key));
  for (const key of found) {
    assert.ok(
      classified.has(key),
      `${key} is used in src/ but has no row in browserStorage.ts -- classify it as ` +
        `"activity" (deleted with the user's activity) or "preference" (kept)`,
    );
  }
  // And the other direction, so a row for a key nobody uses cannot sit here looking like
  // coverage of something.
  for (const key of classified) {
    assert.ok(found.has(key), `${key} is classified but no longer used in src/`);
  }
}

// Every row picks a side, names itself, and explains itself. An "activity" row with no label
// would be deleted without the user being told, which is half the point of the item.
for (const entry of BROWSER_STORAGE_KEYS) {
  assert.ok(
    entry.classification === "activity" || entry.classification === "preference",
    `${entry.key} has no classification`,
  );
  assert.ok(entry.why.length > 0, `${entry.key} does not say why`);
  if (entry.classification === "activity") {
    assert.ok(entry.label.length > 0, `${entry.key} is deleted but unnamed`);
  }
}

// The classification of record, pinned by value. Changing any of these is a privacy decision,
// so it should require editing a test that says so.
assert.deepEqual(
  ACTIVITY_STORAGE_KEYS.map((entry) => entry.key),
  [SESSION_PRESETS_KEY],
);

// --- Clearing --------------------------------------------------------------------------

{
  const storage = new MemoryStorage();
  storage.setItem(SESSION_PRESETS_KEY, JSON.stringify([{ id: "p1", goal: "Write the RFC" }]));
  storage.setItem(APPEARANCE_STORAGE_KEY, "dark");
  storage.setItem(FIRST_RUN_ACK_KEY, "1");
  storage.setItem(ONBOARDING_DONE_KEY, "1");
  storage.setItem(REVIEW_RANGE_STORAGE_KEY, "7d");
  storage.setItem(WORK_APP_TEACH_DONE_KEY, "1");

  const cleared = clearActivityStorage(storage);

  // The goal text is gone. This is the assertion the item exists for: after
  // `delete_all_activity_data` the database holds no goals, so this was the only copy left.
  assert.equal(storage.getItem(SESSION_PRESETS_KEY), null);
  assert.deepEqual(cleared, ["your saved session presets"]);

  // Erasing activity is not resetting the app. Clearing these would re-run onboarding over a
  // working install and throw away a theme choice, neither of which the user asked for.
  assert.equal(storage.getItem(APPEARANCE_STORAGE_KEY), "dark");
  assert.equal(storage.getItem(FIRST_RUN_ACK_KEY), "1");
  assert.equal(storage.getItem(ONBOARDING_DONE_KEY), "1");
  assert.equal(storage.getItem(REVIEW_RANGE_STORAGE_KEY), "7d");
  assert.equal(storage.getItem(WORK_APP_TEACH_DONE_KEY), "1");
}

// Clearing twice is not an error, and the second pass still reports what it removed -- the
// message is about what this side owns, not about whether it happened to be populated.
{
  const storage = new MemoryStorage();
  clearActivityStorage(storage);
  assert.deepEqual(clearActivityStorage(storage), ["your saved session presets"]);
}

// A storage that throws leaves the copy behind, so it must not be claimed as cleared. Saying
// "your presets were cleared" when they were not is the same overstatement 8.12 forbids for
// the native side.
{
  const throwing = {
    removeItem: () => {
      throw new Error("storage disabled");
    },
  };
  assert.deepEqual(clearActivityStorage(throwing), []);
}

// No storage at all (private window, sandboxed frame) is not a failure and not a claim.
assert.deepEqual(clearActivityStorage(null), []);

console.log("browserStorage.test.ts passed");
